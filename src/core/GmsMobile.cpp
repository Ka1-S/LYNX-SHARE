#include "core/GmsMobile.h"

#include "core/PocketTape.h" // round 190dw item 4 (FLNG)
#include "core/Reeverb2.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <mutex>

// FL Studio Mobile's GMSynth, written from the behavioural spec docs/analise/REA_rodada190ab_gmsynth.md (clean-room). The
// module computes in float32 with a few double steps; this file keeps those types and the order of every expression,
// so contraction (a·b + c as one FMA) stays off, like core/Gms.cpp.
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize ("fp-contract=off")
#endif

namespace lynx
{
namespace gmsmobile
{
namespace
{
constexpr int tableCount = 512, tableSize = 1024;
constexpr double twoPi = 6.283185307179586;

// truncation toward zero as cvttss2si / cvttsd2si do it (out of range or NaN → INT_MIN)
int i32 (double x) noexcept
{
    if (! (x == x) || x >= 2147483648.0 || x < -2147483648.0)
        return std::numeric_limits<int>::min();
    return (int) x;
}
std::uint32_t u32of (double x) noexcept // (uint32) of a cvttsd2si into a 64-bit register
{
    if (! (x == x) || x >= 9.2e18 || x <= -9.2e18)
        return 0u;
    return (std::uint32_t) (std::int64_t) x;
}
float powf32 (float x, float y) noexcept { return (float) std::pow ((double) x, (double) y); }
float clamp01 (float x) noexcept // max(x, 0) then min(…, 1), the module's order
{
    if (x <= 0.0f)
        x = 0.0f;
    if (1.0f <= x)
        x = 1.0f;
    return x;
}
double frac (double x) noexcept { return x - (double) i32 (x); }

const char* const waveNames[waveCount] = {
    "Beast", "Crunch", "Devils Horn", "Digital", "Dirt", "Electro", "Filth", "Fox", "Funky", "Grubby", "Keys", "Liquid", "Ogre",
    "Rhodes", "Saw Mean", "Saw Retro", "Saw", "Silk", "Sine", "Smooth Sub", "Square Retro", "Square Smooth", "Square", "Strat",
    "String", "Throat", "Triangle", "Troll", "Vocal"
};

std::vector<std::uint8_t> readFileBytes (std::string path)
{
    std::ifstream in (path, std::ios::binary);
    if (! in)
    {
        std::replace (path.begin(), path.end(), '\\', '/');
        in.open (path, std::ios::binary);
    }
    if (! in)
        return {};
    return std::vector<std::uint8_t> ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
}

std::string joinPath (const std::string& folder, const std::string& rel)
{
    if (folder.empty())
        return rel;
    const auto last = folder.back();
    return last == '/' || last == '\\' ? folder + rel : folder + "/" + rel;
}

// ------------------------------------------------------------------------------------------------- data files
//
// <name>.spectra: 512 magnitudes then 512 phases (f32) of harmonics 1…512. Table k (k + 1 harmonics) =
// Σ_{h ≤ k+1} A_h·cos(2π·h·i/1024 + φ_h), accumulated in float with double products (the module stores them the other
// way round, most harmonics first; the index here is the desktop engine's).
using Tables = std::vector<float>;

std::shared_ptr<const Tables> buildTables (const std::vector<std::uint8_t>& file)
{
    if (file.size() < 4096)
        return nullptr;
    auto T = std::make_shared<Tables> ((std::size_t) tableCount * tableSize, 0.0f);
    std::vector<float> acc (tableSize, 0.0f);
    for (int k = 0; k < tableCount; ++k)
    {
        float mag, ph;
        std::memcpy (&mag, file.data() + 4 * (std::size_t) k, 4);
        std::memcpy (&ph, file.data() + 2048 + 4 * (std::size_t) k, 4);
        const auto h = (double) ((float) k + 1.0f);
        auto* row = T->data() + (std::size_t) k * tableSize;
        for (int i = 0; i < tableSize; ++i)
        {
            acc[(std::size_t) i] = (float) (std::cos ((double) i * h * 0.006135923151542565 + (double) ph) * (double) mag + (double) acc[(std::size_t) i]);
            row[i] = acc[(std::size_t) i];
        }
    }
    return T;
}

std::shared_ptr<const Tables> waveTables (const std::string& root, int index)
{
    static std::mutex mutex;
    static std::map<std::string, std::shared_ptr<const Tables>> cache;
    if (root.empty() || index < 0 || index >= waveCount)
        return nullptr;
    const auto path = joinPath (joinPath (root, "GMSynthWaveshapes"), std::string (waveNames[index]) + ".spectra");
    {
        const std::lock_guard<std::mutex> lock (mutex);
        if (const auto it = cache.find (path); it != cache.end())
            return it->second;
    }
    auto T = buildTables (readFileBytes (path));
    const std::lock_guard<std::mutex> lock (mutex);
    return cache.emplace (path, T).first->second;
}

// FilterTable_Ctf_Res.data: 65 536 (g, k) float pairs; the run-time table's entry i is the file's entry
// filterFileIndex(i) (an exponential cutoff scale), plus one guard pair for the interpolation at the top.
struct FilterTable
{
    std::vector<float> g, k;
};

std::shared_ptr<const FilterTable> filterTable (const std::string& root)
{
    static std::mutex mutex;
    static std::map<std::string, std::shared_ptr<const FilterTable>> cache;
    if (root.empty())
        return nullptr;
    const auto path = joinPath (root, "FilterTable_Ctf_Res.data");
    {
        const std::lock_guard<std::mutex> lock (mutex);
        if (const auto it = cache.find (path); it != cache.end())
            return it->second;
    }
    std::shared_ptr<FilterTable> t;
    if (const auto file = readFileBytes (path); file.size() >= 0x80000)
    {
        t = std::make_shared<FilterTable>();
        t->g.resize (65537);
        t->k.resize (65537);
        for (int i = 0; i < 65536; ++i)
        {
            const auto j = (std::size_t) filterFileIndex (i);
            std::memcpy (&t->g[(std::size_t) i], file.data() + 8 * j, 4);
            std::memcpy (&t->k[(std::size_t) i], file.data() + 8 * j + 4, 4);
        }
        t->g[65536] = t->g[65535];
        t->k[65536] = t->k[65535];
    }
    const std::lock_guard<std::mutex> lock (mutex);
    return cache.emplace (path, t).first->second;
}

// ------------------------------------------------------------------------------------------------------- effects

constexpr float syncBeats[10] = { 0.25f, 0.375f, 0.5f, 0.75f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f };
float syncPeriod (float v) noexcept
{
    const auto k = i32 ((double) (v * 9.0f));
    return k >= 0 && k < 10 ? syncBeats[k] : 1.0f;
}

// One effect slot. FLM keeps ten objects per slot (one per kind) and cross-fades between them when the kind changes;
// a patch from a .flm never changes its kinds, so the slot holds the selected kind's object only.
struct Fx
{
    int kind = 1;
    float fs = 44100.0f, X = 0.5f, Y = 0.5f, period = 0.5f, amount = 0.0f;
    int shape = 0;
    bool on = false;
    float fade = 0.0f;
    double phase = 0.0, inc = 0.0;
    int tail = 0;
    // DIST
    float heldL = 0.0f, heldR = 0.0f, hold = 0.0f;
    // PHAS (6 all-pass stages + the fed-back output), F LP (4 biquads), PAN (two short lines)
    float ap[7][2] {};
    float smoothA = -1.0f, smoothB = -1.0f;
    float x1[2][4] {}, x2[2][4] {}, y1[2][4] {}, y2[2][4] {};
    std::vector<float> lineL, lineR;
    int writeL = 0, writeR = 0;
    // DLY: two lines of 1.5 s, the fed-back signal's low-pass and high-pass states, the silent-sample counter
    std::vector<float> delayL, delayR;
    int delayAt = 0, delayLength = -1, silent = 0;
    float lp[2] {}, hp[2] {};
    // RVRB: FLM's plate (its copy of the one behind FL's Fruity Reeverb 2), made off the audio thread (ensureReverb)
    std::unique_ptr<reeverb2::Engine> reverb;
    reeverb2::Settings reverbSet;
    int reverbRate = 0;
    // --- round 190dw item 4: FLNG, F HP, RING, VOX (FLM_FORMAT §7.9)
    // FLNG: FLM's tape of ⌊fs·0.005⌋ samples (the MultiFX flanger's, core/PocketTape.h); RING: its oscillator phase;
    // RING / VOX: FLM's second gain, reset to 0 and gliding to 1 by 0.005 per processed sample; VOX: eight constant-skirt
    // band-passes per side (F HP keeps its four biquads in x1 … y2, as F LP does)
    pocket::tape::Tape tape;
    float ringPhase = 0.0f, mixNow = 0.0f;
    struct Band
    {
        float f = -1.0f, q = -1.0f, rate = -1.0f, b0 = 0.0f, c1 = 0.0f, c2 = 0.0f, x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;
    };
    Band vox[2][8];
    // --- end of round 190dw item 4

    void prepare (float rate)
    {
        fs = rate;
        tape.allocate (std::max (4, i32 ((double) (fs * 0.005f)))); // round 190dw item 4
        const auto n = std::max (8, i32 ((double) fs * 0.002));
        lineL.assign ((std::size_t) n, 0.0f);
        lineR.assign ((std::size_t) n, 0.0f);
        const auto d = std::max (2, i32 ((double) (fs * 1.5f)));
        delayL.assign ((std::size_t) d, 0.0f);
        delayR.assign ((std::size_t) d, 0.0f);
        phase = 0.0;
        reset();
    }
    void reset() noexcept
    {
        std::fill (lineL.begin(), lineL.end(), 0.0f);
        std::fill (lineR.begin(), lineR.end(), 0.0f);
        writeL = writeR = 0;
        std::memset (ap, 0, sizeof (ap));
        std::memset (x1, 0, sizeof (x1));
        std::memset (x2, 0, sizeof (x2));
        std::memset (y1, 0, sizeof (y1));
        std::memset (y2, 0, sizeof (y2));
        heldL = heldR = hold = 0.0f;
        clearDelay();
        if (reverb != nullptr)
            reverb->flush();
        silent = on ? 0 : i32 ((double) (fs * 10.0f));
        fade = on ? 1.0f : 0.0f;
        smoothA = smoothB = -1.0f;
        tail = i32 ((double) (fs * 10.0f));
        // round 190dw item 4 (FLM's reset 0x1806db5b0: the tape cleared, the band states and the ring phase zeroed, the
        // second gain back to 0)
        tape.clear();
        tape.fb = 0.0f;
        ringPhase = 0.0f;
        mixNow = 0.0f;
        for (auto& side : vox)
            for (auto& b : side)
                b.x1 = b.x2 = b.y1 = b.y2 = 0.0f;
    }
    // (allocates: prepare / setPatch) the plate for a slot whose kind is RVRB
    void ensureReverb()
    {
        if (kind != fx::reverb || (reverb != nullptr && reverbRate == i32 ((double) fs)))
            return;
        reverbRate = i32 ((double) fs);
        reverb = std::make_unique<reeverb2::Engine>();
        reverbSet = reverbSettings();
        reverb->reserve (reverbRate, 64);
        reverb->prepare (reverbRate, reverbSet, 120.0);
    }
    // FLM's integer control for a 0…1 value: ⌊lo + v·(hi − lo) + ½⌋
    static int control (float v, float lo, float hi) noexcept
    {
        v = std::min (std::max (v, 0.0f), 1.0f);
        return (int) std::floor ((hi - lo) * v + lo + 0.5f);
    }
    float reverbAmount() const noexcept { return (X * X) * (X * 0.75f); }
    // decay ⌊1 + 199·0.75·X³ + ½⌋ tenths of a second, damping ⌊5 + 216·Y² + ½⌋ (× 100 Hz), low cut 317 Hz, no high
    // cut, room 100, diffusion 100, no predelay, no dry, no early reflections; bass ×1.0 and crossover 500 Hz are the
    // engine's defaults. The wet level is applied outside (wetGain), on the engine's highest wet setting.
    reeverb2::Settings reverbSettings() const noexcept
    {
        reeverb2::Settings s;
        s.p = { control (0.1f, 19.0f, 3000.0f), 221, 0, 100, 100, control (reverbAmount(), 1.0f, 200.0f), control (Y * Y, 5.0f, 221.0f), 100, 500, 0, 0, 0,
                160, 684, 0 };
        return s;
    }
    // the wet path: 0.3·10^(t/200), t = ⌊−361 + 421·(0.5 + 0.2·0.75·X³) + ½⌋ tenths of a dB (−361 = off), over the
    // engine's gain at its wet setting 160
    float wetGain() const noexcept
    {
        const auto t = control (reverbAmount() * 0.2f + 0.5f, -361.0f, 60.0f);
        if (t <= -361)
            return 0.0f;
        return (float) (0.3 * std::pow (10.0, (double) t * 0.005) / ((std::exp (2.3978953 * 160.0 / 256.0) - 1.0) * 0.1));
    }
    void clearDelay() noexcept
    {
        std::fill (delayL.begin(), delayL.end(), 0.0f);
        std::fill (delayR.begin(), delayR.end(), 0.0f);
        delayAt = 0;
        delayLength = -1;
        lp[0] = lp[1] = hp[0] = hp[1] = 0.0f;
    }
    void setPeriod (float beats) noexcept
    {
        period = kind == fx::delay ? beats + beats : beats;
        if (period < 0.0f)
            period = 0.0f;
    }

    // one LFO value (the effect reads it once per chunk), then the phase moves on by the rest of the chunk
    float lfo (int n) noexcept
    {
        float v;
        if (shape == 0)
        {
            const auto a = phase * twoPi;
            phase = frac (phase + inc);
            v = (float) std::sin (a) * amount;
        }
        else if (shape == 1)
        {
            phase = frac (inc + phase);
            v = (float) (1.0 - (phase + phase)) * amount;
        }
        else
        {
            const auto r = phase > 0.5 ? -1.0f : 1.0f;
            phase = frac (phase + inc);
            v = r * amount;
        }
        phase = frac ((double) (n - 1) * inc + phase);
        return v;
    }

    // (DLY also runs on while its echoes sound: up to 1.5 s of silence)
    bool running() const noexcept { return on || 0.0f < fade || (kind == fx::delay && (float) silent <= fs * 1.5f); }

    // false = the slot gave nothing new (the caller's "something sounds" flag stays as it was)
    bool process (float* L, float* R, int n, float tempo, bool input) noexcept
    {
        if (fs < 1.0f || tempo < 1.0f)
            return false;
        const auto seconds = period * 60.0f / tempo;
        inc = seconds == 0.0f ? 0.0 : (double) (1.0f / (seconds * fs));
        const auto target = on ? 1.0f : 0.0f;
        switch (kind)
        {
            case fx::dist: return on && dist (L, R, n, input);
            case fx::phaser: return running() && phaser (L, R, n, input, target);
            case fx::lowPass: return running() && lowPass (L, R, n, input, target);
            case fx::pan: return running() && panner (L, R, n, input, target);
            case fx::delay: return running() && delay (L, R, n, seconds, input, target);
            case fx::reverb: return running() && reverb != nullptr && reverbFx (L, R, n, input, target);
            // round 190dw item 4
            case fx::flanger: return running() && flanger (L, R, n, input, target);
            case fx::highPass: return running() && highPass (L, R, n, input, target);
            case fx::ring: return running() && ringMod (L, R, n, input, target);
            case fx::vox: return running() && voxFx (L, R, n, input, target);
            default: return false;
        }
    }

    // DIST: a sample-and-hold of Y⁴·256 + 1 samples around the shaper x/(|x|·k + 1)·(0.1 − 0.09·Y)·(k + 1),
    // k = 10^(4·(X + lfo) + 1); no cross-fade
    bool dist (float* L, float* R, int n, bool input) noexcept
    {
        fade = 1.0f;
        auto y4 = Y * Y;
        y4 = y4 * (Y * Y);
        const auto step = clamp01 (y4) * 256.0f + 1.0f;
        const auto k = powf32 (10.0f, (lfo (n) + X) * 4.0f + 1.0f);
        const auto gain = (0.1f - Y * 0.089999996f) * (k + 1.0f);
        for (int i = 0; i < n; ++i)
        {
            auto c = hold;
            if (! (0.0f < c))
            {
                c = c + step;
                heldL = (L[i] / (std::fabs (L[i]) * k + 1.0f)) * gain;
                heldR = (R[i] / (std::fabs (R[i]) * k + 1.0f)) * gain;
            }
            hold = c - 1.0f;
            L[i] = heldL;
            R[i] = heldR;
        }
        return input;
    }

    // DLY: two lines of period / 2 seconds (the object's period is twice the slot's; halved while above 1.5 s), fed by
    // fade·input + the line's own output·X² through a one-pole low-pass (Y below ½: coefficient 0.01 + 0.99·2Y) and,
    // one sample later, a one-pole high-pass (Y above ½: coefficient 0.45·(2Y − 1)); the fed-back signal is the wet
    // output, added to the input at 1 ± lfo per side (the LFO runs per sample here). A new length clears the lines.
    bool delay (float* L, float* R, int n, float seconds, bool input, float target) noexcept
    {
        if (! input && fs * 1.5f < (float) silent)
        {
            fade = target;
            return false;
        }
        const auto feedback = X * X;
        auto tone = std::min (std::max (Y, 0.0f), 1.0f);
        tone = tone + tone;
        auto high = 0.0f;
        if (1.0f < tone)
        {
            high = tone - 1.0f;
            tone = 1.0f;
        }
        high = high * 0.45f;
        const auto low = tone * 0.99f + 0.01f;
        auto part = (double) (seconds * 0.33333334f);
        while (1.0 < part)
            part = part * 0.5;
        const auto size = (int) delayL.size();
        auto length = i32 ((double) (size - 1) * part);
        if (delayLength != -1 && length != delayLength)
            clearDelay();
        delayLength = length;
        length = std::min (std::max (length, 1), size - 1);
        for (int i = 0; i < n; ++i)
        {
            if (length <= delayAt)
                delayAt = 0;
            float pan;
            if (shape == 0)
            {
                pan = (float) std::sin (phase * twoPi);
                phase = frac (phase + inc);
            }
            else if (shape == 1)
            {
                phase = frac (phase + inc);
                pan = (float) (1.0 - (phase + phase));
            }
            else
            {
                pan = phase > 0.5 ? -1.0f : 1.0f;
                phase = frac (phase + inc);
            }
            pan = pan * amount;
            fade = fade + (target - fade) * 0.005f;
            const auto at = (std::size_t) delayAt;
            auto wetL = lp[0], wetR = lp[1];
            lp[0] = lp[0] + (delayL[at] * feedback - lp[0]) * low;
            lp[1] = lp[1] + (delayR[at] * feedback - lp[1]) * low;
            hp[0] = hp[0] + (wetL - hp[0]) * high;
            hp[1] = hp[1] + (wetR - hp[1]) * high;
            wetL = wetL - hp[0];
            wetR = wetR - hp[1];
            delayL[at] = fade * L[i] + wetL;
            delayR[at] = fade * R[i] + wetR;
            ++delayAt;
            if (1.0e-4f < std::fabs (wetL) || 1.0e-4f < std::fabs (wetR))
                silent = 0;
            else
                ++silent;
            L[i] = (pan + 1.0f) * wetL + L[i];
            R[i] = (1.0f - pan) * wetR + R[i];
        }
        if (1.0e-4f <= fade || (float) silent <= fs * 1.5f)
            return true;
        const auto keep = silent;
        clearDelay();
        silent = keep;
        return false;
    }

    // RVRB: the plate's wet output for fade·input, added to the signal; it stops 0.25 s after the output went silent
    bool reverbFx (float* L, float* R, int n, bool input, float target) noexcept
    {
        if (! input && fs * 0.25f < (float) silent)
        {
            fade = target;
            return false;
        }
        if (const auto s = reverbSettings(); ! (s == reverbSet))
        {
            reverbSet = s;
            reverb->set (s, 120.0);
        }
        const auto gain = wetGain();
        float wl[64], wr[64];
        for (int at = 0; at < n; at += 64)
        {
            const auto m = std::min (64, n - at);
            for (int i = 0; i < m; ++i)
            {
                fade = fade + (target - fade) * 0.005f;
                wl[i] = fade * L[at + i];
                wr[i] = fade * R[at + i];
            }
            reverb->process (wl, wr, m);
            for (int i = 0; i < m; ++i)
            {
                L[at + i] = L[at + i] + wl[i] * gain;
                R[at + i] = R[at + i] + wr[i] * gain;
                if (1.0e-4f < std::fabs (L[at + i]) || 1.0e-4f < std::fabs (R[at + i]))
                    silent = 0;
                else
                    ++silent;
            }
        }
        if (on || 1.0e-4f <= fade || (float) silent <= fs * 0.25f)
            return true;
        const auto keep = silent;
        reset();
        silent = keep;
        return false;
    }

    // the common head of the cross-faded kinds: with no input the object runs on for `limit` samples
    bool head (int n, bool input, float limit, float target) noexcept
    {
        if (! input)
        {
            if (limit < (float) tail)
            {
                fade = target;
                return false;
            }
            tail += n;
        }
        else
            tail = 0;
        return true;
    }
    bool finish() noexcept
    {
        if (on || 1.0e-4f <= fade)
            return true;
        reset();
        return false;
    }

    // PHAS: six first-order all-pass stages, the last output fed back by 0.98·(0.6·Y)^(1/3), added to the dry signal;
    // corner (100 + X²·(1 + lfo)·39 900) Hz
    bool phaser (float* L, float* R, int n, bool input, float target) noexcept
    {
        if (! head (n, input, fs * 0.25f, target))
            return false;
        const auto h = (double) (0.5f / fs);
        const auto lo = (float) (h * 100.0);
        const auto fb = (float) ((double) powf32 (Y * 0.6f, 0.33333334f) * 0.98);
        const auto l = lfo (n);
        auto w = X * X * (l + 1.0f) * ((float) (h * 40000.0) - lo) + lo;
        if (! (0.001 <= (double) w))
            w = 0.001f;
        else if (0.499 < (double) w)
            w = 0.499f;
        const auto a = (1.0f - w) / (w + 1.0f), na = -a;
        for (int i = 0; i < n; ++i)
        {
            fade = (target - fade) * 0.005f + fade;
            float x[2] = { (float) ((double) (fade * L[i]) + 1.0e-30) + fb * ap[6][0], (float) ((double) (fade * R[i]) + 1.0e-30) + fb * ap[6][1] };
            for (int s = 0; s < 6; ++s)
                for (int c = 0; c < 2; ++c)
                {
                    const auto y = na * x[c] + ap[s][c];
                    ap[s][c] = y * a + x[c];
                    x[c] = y;
                }
            ap[6][0] = x[0];
            ap[6][1] = x[1];
            L[i] = x[0] * 1.0f + L[i];
            R[i] = x[1] * 1.0f + R[i];
        }
        return finish();
    }

    // F LP: four identical low-pass biquads in series (one sample of delay between them), cutoff
    // 2^(8.28·(X + lfo) + 6) Hz, q = 2Y² + 1; cross-faded against the dry signal
    bool lowPass (float* L, float* R, int n, bool input, float target) noexcept
    {
        if (! head (n, input, fs, target))
            return false;
        const auto l = lfo (n);
        const auto q = (Y + Y) * Y + 1.0f;
        const auto f = powf32 (2.0f, clamp01 (l + X) * 8.28f + 6.0f);
        const auto w = (float) ((double) f * twoPi / (double) fs);
        const auto s = (float) std::sin ((double) w), c = (float) std::cos ((double) w);
        const auto tiny = [] (float v) { return v < 9.999999717180685e-10f && v > -9.999999717180685e-10f ? 0.0f : v; };
        const auto inv = 1.0f / (s + q);
        const auto b = tiny ((1.0f - c) / (s / q + 2.0f));
        const auto a1 = tiny (c * inv * (q * -2.0f));
        const auto a2 = tiny ((q - s) * inv);
        float* io[2] = { L, R };
        for (int i = 0; i < n; ++i)
        {
            fade = (target - fade) * 0.005f + fade;
            for (int ch = 0; ch < 2; ++ch)
            {
                const auto x = io[ch][i] + 1.0000000031710769e-30f;
                float in[4] = { x, y1[ch][0], y1[ch][1], y1[ch][2] }, y[4];
                for (int j = 0; j < 4; ++j)
                    y[j] = ((x1[ch][j] * 2.0f + in[j] + x2[ch][j]) * b - y1[ch][j] * a1) - a2 * y2[ch][j];
                for (int j = 0; j < 4; ++j)
                {
                    x2[ch][j] = x1[ch][j];
                    x1[ch][j] = in[j];
                    y2[ch][j] = y1[ch][j];
                    y1[ch][j] = y[j];
                }
                io[ch][i] = (1.0f - fade) * x + fade * y[3];
            }
        }
        return finish();
    }

    // PAN: position X + lfo; the far side is delayed by up to (fs·0.002 − 4) samples and turned down by √Y
    bool panner (float* L, float* R, int n, bool input, float target) noexcept
    {
        if (! head (n, input, fs, target))
            return false;
        const auto depth = std::sqrt (Y);
        const auto pos = lfo (n) + X;
        if (smoothA < 0.0f)
            smoothA = pos;
        if (smoothB < 0.0f)
            smoothB = depth;
        const auto size = (int) lineL.size();
        const auto span = (float) (size - 4);
        const auto tap = [size] (const std::vector<float>& line, int write, float delay) {
            const auto whole = i32 ((double) delay);
            auto at = write - whole;
            if (at < 0)
                at += size;
            auto before = at - 1;
            if (before < 0)
                before += size;
            const auto fr = delay - (float) whole;
            const auto v = line[(std::size_t) std::clamp (at, 0, size - 1)];
            return fr * line[(std::size_t) std::clamp (before, 0, size - 1)] + (v - fr * v);
        };
        for (int i = 0; i < n; ++i)
        {
            const auto inL = L[i], inR = R[i];
            if (writeL >= size)
                writeL = 0;
            if (writeR >= size)
                writeR = 0;
            lineL[(std::size_t) writeL] = inL;
            fade = fade + (target - fade) * 0.005f;
            smoothA = smoothA + (pos - smoothA) * 0.005f;
            smoothB = smoothB + (depth - smoothB) * 0.005f;
            const auto p = clamp01 (smoothA);
            auto right = (p + p) - 1.0f, left = ((1.0f - p) + (1.0f - p)) - 1.0f;
            if (right <= 0.0f)
                right = 0.0f;
            if (left <= 0.0f)
                left = 0.0f;
            const auto wetL = tap (lineL, writeL, span * right + 1.0f);
            ++writeL;
            lineR[(std::size_t) writeR] = inR;
            const auto wetR = tap (lineR, writeR, span * left + 1.0f);
            ++writeR;
            L[i] = wetL * fade * (-smoothB * right + 1.0f) + (inL - fade * inL);
            R[i] = wetR * fade * (-smoothB * left + 1.0f) + (inR - fade * inR);
        }
        if ((float) tail <= fs)
            return true;
        reset();
        return false;
    }

    // --- round 190dw item 4 (FLM's GMSynth effect processors, PROVEN-REA; FLM_FORMAT §7.9)
    // FLNG (inline in the dispatcher 0x1806db820): the mono −(L + R)/2 × fade through FLM's tape, feedback −0.96·Y,
    // delay (1 − (X + lfo/2))⁴ (clamped 0…1) of the tape, added to both sides; runs on 0.25 s after its input
    bool flanger (float* L, float* R, int n, bool input, float target) noexcept
    {
        if (! head (n, input, fs * 0.25f, target))
            return false;
        const auto fb = (float) ((double) Y * -0.96);
        const auto l = lfo (n);
        auto d = 1.0f - (l * 0.5f + X);
        d = d * d;
        d = d * d;
        if (d <= 0.0f)
            d = 0.0f;
        if (1.0f <= d)
            d = 1.0f;
        tape.target = 1.0 / ((double) d * (double) (tape.n - 2) + 2.0);
        tape.fb = fb;
        for (int i = 0; i < n; ++i)
        {
            fade = (target - fade) * 0.005f + fade;
            const auto y = tape.tick ((L[i] * -0.5f - R[i] * 0.5f) * fade);
            R[i] = y + R[i];
            L[i] = y + L[i];
        }
        return finish();
    }

    // F HP (0x1806dc340): four identical high-pass biquads in series (one sample of delay between them), cutoff
    // 2^(7.424·(X + lfo) + 5) Hz, q = 2Y² + 1, FLM's high-pass design 0x18060d940 (feed-forward (1 + cos)/(sin/q + 2),
    // feedback as F LP's); cross-faded against the dry signal
    bool highPass (float* L, float* R, int n, bool input, float target) noexcept
    {
        if (! head (n, input, fs, target))
            return false;
        const auto l = lfo (n);
        const auto q = (Y + Y) * Y + 1.0f;
        const auto f = powf32 (2.0f, clamp01 (l + X) * 7.424f + 5.0f);
        const auto w = (float) ((double) f * twoPi / (double) fs);
        const auto s = (float) std::sin ((double) w), c = (float) std::cos ((double) w);
        const auto tiny = [] (float v) { return v < 9.999999717180685e-10f && v > -9.999999717180685e-10f ? 0.0f : v; };
        const auto inv = 1.0f / (s + q);
        const auto b = tiny ((c + 1.0f) / (s / q + 2.0f));
        const auto a1 = tiny (c * inv * (q * -2.0f));
        const auto a2 = tiny ((q - s) * inv);
        float* io[2] = { L, R };
        for (int i = 0; i < n; ++i)
        {
            fade = (target - fade) * 0.005f + fade;
            for (int ch = 0; ch < 2; ++ch)
            {
                const auto x = io[ch][i] + 1.0000000031710769e-30f;
                float in[4] = { x, y1[ch][0], y1[ch][1], y1[ch][2] }, y[4];
                for (int j = 0; j < 4; ++j)
                    y[j] = ((x1[ch][j] * -2.0f + in[j] + x2[ch][j]) * b - y1[ch][j] * a1) - a2 * y2[ch][j];
                for (int j = 0; j < 4; ++j)
                {
                    x2[ch][j] = x1[ch][j];
                    x1[ch][j] = in[j];
                    y2[ch][j] = y1[ch][j];
                    y1[ch][j] = y[j];
                }
                io[ch][i] = (1.0f - fade) * x + fade * y[3];
            }
        }
        return finish();
    }

    // RING (0x1806ddfc0): a sine of 2^(8.7·(X + lfo/2) + 5) Hz (32 Hz … 13.3 kHz; float phase) times the signal,
    // cross-faded against the dry by fade × the second gain; Y is not used
    bool ringMod (float* L, float* R, int n, bool input, float target) noexcept
    {
        if (! head (n, input, fs * 0.25f, target))
            return false;
        const auto u = std::min (std::max (lfo (n) * 0.5f + X, 0.0f), 1.0f);
        const auto inc = powf32 (2.0f, u * 8.7f + 5.0f) / fs;
        for (int i = 0; i < n; ++i)
        {
            mixNow = (1.0f - mixNow) * 0.005f + mixNow;
            fade = (target - fade) * 0.005f + fade;
            const auto g = fade * mixNow;
            auto ph = inc + ringPhase;
            ph = ph - (float) i32 ((double) ph);
            ringPhase = ph;
            const auto s = (float) std::sin ((double) ph * twoPi);
            R[i] = s * R[i] * g + (1.0f - g) * R[i];
            L[i] = s * L[i] * g + (1.0f - g) * L[i];
        }
        return finish();
    }

    // VOX (0x1806dda30): eight constant-skirt band-passes (Q 12, design 0x18060dc80 in double) per side on three formant
    // sets blended by 3·(X + lfo): A → B → C → A, the eight centres × 2^(1.7·(Y − ½)); the sum × (1 − k/8) for the
    // k-th pair, cross-faded against the dry by fade × the second gain
    void voxCoefficients (Band& b, float f, float q) noexcept
    {
        if (f == b.f && q == b.q && fs == b.rate)
            return;
        b.f = f;
        b.q = q;
        b.rate = fs;
        const auto w = (double) f * twoPi / (double) fs;
        const auto s = std::sin (w), c = std::cos (w);
        const auto k = 1.0 / ((0.5 / (double) q) * s + 1.0);
        const auto tiny = [] (float v) { return v < 9.999999717180685e-10f && v > -9.999999717180685e-10f ? 0.0f : v; };
        b.b0 = tiny ((float) ((double) (float) (s * 0.5) * k));
        b.c1 = tiny ((float) ((double) (float) (c + c) * k));
        b.c2 = tiny ((float) ((double) (float) (1.0 - s / (double) (q + q)) * k));
    }
    bool voxFx (float* L, float* R, int n, bool input, float target) noexcept
    {
        static constexpr float setA[8] { 250.0f, 700.0f, 2500.0f, 3500.0f, 4200.0f, 8500.0f, 10000.0f, 12000.0f };
        static constexpr float setB[8] { 700.0f, 1250.0f, 2500.0f, 3500.0f, 4200.0f, 8500.0f, 10000.0f, 12000.0f };
        static constexpr float setC[8] { 250.0f, 2500.0f, 2700.0f, 3500.0f, 4200.0f, 8500.0f, 10000.0f, 12000.0f };
        if (! head (n, input, fs * 0.25f, target))
            return false;
        const auto v = std::min (std::max (lfo (n) + X, 0.0f), 1.0f) * 3.0f;
        const auto shift = powf32 (2.0f, (Y - 0.5f) * 1.7f);
        const auto k = i32 ((double) v);
        const auto pair = k < 3 ? k : 0;
        const auto frac = v - (float) k;
        float wA = 0.0f, wB = 0.0f, wC = 0.0f;
        if (pair == 0)
            wA = 1.0f - frac, wB = frac;
        else if (pair == 1)
            wB = 1.0f - frac, wC = frac;
        else
            wA = frac, wC = 1.0f - frac;
        const auto gain = 1.0f - (float) pair * 0.125f;
        for (auto& side : vox)
            for (int j = 0; j < 8; ++j)
                voxCoefficients (side[j], ((wB * setB[j] + wA * setA[j]) + wC * setC[j]) * shift, 12.0f);
        const auto tiny = [] (float x) { return x < 9.999999717180685e-10f && x > -9.999999717180685e-10f ? 0.0f : x; };
        float* io[2] = { L, R };
        for (int i = 0; i < n; ++i)
        {
            mixNow = (1.0f - mixNow) * 0.005f + mixNow;
            fade = (target - fade) * 0.005f + fade;
            const auto g = fade * mixNow;
            for (int ch = 0; ch < 2; ++ch)
            {
                const auto dry = io[ch][i];
                const auto x = tiny (dry);
                float sum = 0.0f;
                for (auto& b : vox[ch])
                {
                    const auto y = tiny (((x - b.x2) * b.b0 + b.y1 * b.c1) - b.c2 * b.y2);
                    b.x2 = b.x1;
                    b.x1 = x;
                    b.y2 = b.y1;
                    b.y1 = y;
                    sum = sum + y * gain;
                }
                io[ch][i] = (1.0f - g) * dry + sum * g;
            }
        }
        return finish();
    }
    // --- end of round 190dw item 4
};

// ------------------------------------------------------------------------------------------------------- voice

struct Voice
{
    float vel = 0.0f, pan = 0.5f;
    int key = -1;
    bool released = false, restart = false, silent = false;
    std::uint32_t order = 0;
    float velTarget = 0.0f, glide = -1.0f;
    int tabA = 0, tabB = 0;
    std::uint32_t phA[4] {}, incA[4] {}, phB[4] {}, incB[4] {};
    float ampRate = 0.0f, ampTarget = 5.0f, egRate = 0.0f, egTarget = 5.0f;
    float cutTarget = 0.0f, negRes = 0.0f;
    int filterKind = 0, modKind = 0;
    float fenvRate = 0.0f, fenvTarget = 5.0f, fenvAmount = 0.0f;
    float oscBase = 0.0f, menvRate = 0.0f, menvTarget = 0.0f, menvAmount = 0.0f;
    float panL = 1.0f, panR = 1.0f, pitchOfs = 0.0f, panOfs = 0.0f, velOfs = 0.0f;
    // per-sample state
    float y[8][2] {};
    float fenv = 0.0f, amp = 0.0f, menv = 0.0f, eg = 0.0f, velSm = -1.0f, cutSm = -1.0f, modSm = -1.0f;
};
} // namespace

// ------------------------------------------------------------------------------------------------------- engine

struct Player::Impl
{
    double fs = 44100.0;
    std::array<float, paramCount> P {};
    Patch patch;
    std::shared_ptr<const Tables> tabA, tabB;
    std::shared_ptr<const FilterTable> filter;
    std::uint32_t rng = 0;
    Voice voices[voiceCount];
    Fx fxs[3];
    float keyVel[128] {};
    float lastNote = -1.0f, pitchOffset = 0.0f;
    std::uint32_t counter = 0;
    bool started = false, lastOut = false;
    // derived by the parameter setter
    float ampAttack = 1.0f, ampDecay = 0.0f, ampSustain = 1.0f, ampRelease = 0.0f, ratioB = 1.0f;
    float menvAttack = 1.0f, menvDecay = 0.0f, fenvAttack = 1.0f, fenvDecay = 0.0f;
    float egAttack = 1.0f, egDecay = 0.0f, egSustain = 1.0f, egRelease = 0.0f, egContour = 0.0f, egAmount = 0.0f;
    int egDest = 0, lfoShape = 0, lfoDest = 0;
    float lfoPeriod = 32.0f, lfoA = 0.0f, lfoAmount = 0.0f;
    double lfoPhase = 0.0;
    float bendSemis = 2.0f, bend = 0.0f, modWheel = 0.0f, mwAmount = 0.5f;
    int mwDest = 2;
    // per chunk
    float lfoBuf[chunkSize] {}, bufL[chunkSize] {}, bufR[chunkSize] {};
    float mix[4][4] {};
    int choir = 1;
    float noise = 0.0f;
    float gainL = -1.0f, gainR = -1.0f;

    void resetVoice (Voice& v) noexcept
    {
        v.key = -1;
        v.vel = 0.0f;
        std::memset (v.y, 0, sizeof (v.y));
        v.fenv = v.amp = v.menv = v.eg = 0.0f;
        v.velSm = v.cutSm = v.modSm = -1.0f;
        v.ampRate = 0.0f;
        v.ampTarget = 5.0f;
        v.egRate = 0.0f;
        v.egTarget = 5.0f;
        for (int u = 0; u < 4; ++u)
            v.incA[u] = v.incB[u] = 0;
        for (int u = 0; u < 4; ++u)
        {
            rng = rng * 0x53c29b01u + 0xa7deab40u;
            rng = rng * 0x53c29b01u + 0xa7deab40u;
            v.phA[u] = rng;
        }
        v.fenvRate = 0.0f;
        v.fenvTarget = 5.0f;
        v.glide = -1.0f;
        v.panL = v.panR = 1.0f;
    }

    void reset()
    {
        rng = 0;
        lastNote = -1.0f;
        std::fill (std::begin (keyVel), std::end (keyVel), 0.0f);
        for (auto& v : voices)
        {
            v = Voice {};
            resetVoice (v);
        }
        counter = 0;
        for (auto& f : fxs)
            f.prepare ((float) fs);
        lfoPhase = 0.0;
        gainL = gainR = -1.0f;
        started = lastOut = false;
    }

    void applyAll() noexcept
    {
        for (int i = 0; i < paramCount; ++i)
            derive (i, P[(std::size_t) i]);
        bendSemis = 2.0f;
        switch (i32 ((double) std::round (patch.bendRange * 3.0f)))
        {
            case 0: bendSemis = 1.0f; break;
            case 1: bendSemis = 2.0f; break;
            case 2: bendSemis = 7.0f; break;
            case 3: bendSemis = 12.0f; break;
            default: break;
        }
        mwDest = i32 ((double) (patch.mwDest + patch.mwDest));
        mwAmount = patch.mwAmount;
    }

    // the module's parameter setter: what it computes when a parameter is written
    void derive (int k, float v) noexcept
    {
        using namespace param;
        switch (k)
        {
            case param::ampAttack: this->ampAttack = attackRate (v, fs); break;
            case param::ampDecay: this->ampDecay = decayRate (v, fs); break;
            case param::ampSustain: this->ampSustain = v * v; break;
            case param::ampRelease: this->ampRelease = ampReleaseRate (v, fs); break;
            case oscBPitch:
            case oscBDetune:
            {
                const auto semis = P[oscBPitch] * 48.0f - 24.0f;
                ratioB = (float) std::pow (2.0, ((double) (P[oscBDetune] - 0.5f) + (double) i32 ((double) semis)) * 0.08333333333333333);
                break;
            }
            case oscModAttack: menvAttack = attackRate (v, fs); break;
            case oscModDecay: menvDecay = decayRate (v, fs); break;
            case filterAttack: fenvAttack = attackRate (v, fs); break;
            case filterDecay: fenvDecay = decayRate (v, fs); break;
            case lfoRate: lfoPeriod = syncPeriod (v); break;
            case param::lfoShape: this->lfoShape = i32 ((double) (v + v)); break;
            case param::lfoAmount: lfoA = (v + v) - 1.0f; break;
            case param::lfoDest: this->lfoDest = i32 ((double) (v * 4.0f)); break;
            case param::egAttack: this->egAttack = attackRate (v, fs); break;
            case param::egDecay: this->egDecay = decayRate (v, fs); break;
            case param::egSustain: this->egSustain = v * v; break;
            case param::egRelease: this->egRelease = egReleaseRate (v, fs); break;
            case param::egContour: this->egContour = (v + v) - 1.0f; break;
            case param::egDest: this->egDest = i32 ((double) (v + v)); break;
            default: break;
        }
        if (k >= fxBase && k < fxBase + 3 * fxStride)
        {
            auto& f = fxs[(k - fxBase) / fxStride];
            const auto base = fxBase + fxStride * ((k - fxBase) / fxStride);
            switch ((k - fxBase) % fxStride)
            {
                case fxKind:
                    if (const auto kind = fxKindOf (v); kind != f.kind)
                    {
                        f.kind = kind;
                        f.setPeriod (syncPeriod (P[(std::size_t) (base + fxSync)]));
                        f.reset();
                    }
                    break;
                case fxEnable:
                    if (const auto on = v != 0.0f; on != f.on)
                    {
                        f.on = on;
                        if (! started)
                            f.fade = on ? 1.0f : 0.0f;
                    }
                    break;
                case fxSync: f.setPeriod (syncPeriod (v)); break;
                case fxShape: f.shape = std::clamp (i32 ((double) (v + v)), 0, 3); break;
                case fxAmount:
                {
                    const auto a = (v + v) - 1.0f;
                    f.amount = a < 0.0f ? -(a * a) : a * a;
                    break;
                }
                case fxX: f.X = clamp01 (v); break;
                case fxY: f.Y = clamp01 (v); break;
                default: break;
            }
        }
    }

    void setParam (int k, float v) noexcept
    {
        if (k < 0 || k >= paramCount)
            return;
        const auto old = P[(std::size_t) k];
        P[(std::size_t) k] = v;
        if (k == param::singleVoice && v != 0.0f && old == 0.0f) // Single Voice switched on lets every key go
            for (int key = 0; key < 128; ++key)
                note (key, 0.0f, 0.5f, 0.0f);
        derive (k, v);
    }

    void release (Voice& v) noexcept
    {
        if (0.0f < v.vel)
        {
            v.released = true;
            v.ampTarget = 0.0f;
            v.ampRate = ampRelease;
            v.egTarget = 0.0f;
            v.egRate = egRelease;
        }
    }

    void note (int key, float velocity, float pan, float pitch) noexcept
    {
        using namespace param;
        if (key < 0 || key > 127)
            return;
        if (velocity != 0.0f && P[enable] < 0.5f)
            return;
        keyVel[key] = velocity;
        if (velocity == 0.0f)
        {
            for (auto& v : voices)
            {
                if (v.key != key)
                    continue;
                if (P[singleVoice] == 0.0f)
                {
                    release (v);
                    continue;
                }
                // Single Voice: back to the highest key still held below, else the lowest above
                int below = -1, above = -1;
                for (int k = 0; k < key; ++k)
                    if (keyVel[k] != 0.0f)
                        below = k;
                for (int k = 127; k > key; --k)
                    if (keyVel[k] != 0.0f)
                        above = k;
                const auto to = below >= 0 ? below : above;
                if (to < 0)
                    release (v);
                else
                {
                    v.key = to;
                    v.vel = keyVel[to];
                    lastNote = (float) to;
                }
            }
            return;
        }
        started = true;
        Voice* v = &voices[0];
        if (P[singleVoice] == 0.0f)
        {
            Voice* free = nullptr;
            for (auto& c : voices) // the last free voice
                if (! (0.0f < c.vel))
                    free = &c;
            if (free == nullptr)
            {
                free = &voices[0]; // else the oldest (ties: the later voice)
                for (int i = 1; i < voiceCount; ++i)
                    if (voices[i].order <= free->order)
                        free = &voices[i];
            }
            v = free;
        }
        v->order = ++counter;
        v->glide = lastNote;
        if (P[autoPortamento] != 0.0f)
        {
            bool other = false;
            for (int k = 0; k < 128 && ! other; ++k)
                other = keyVel[k] != 0.0f && k != key;
            if (! other)
                v->glide = (float) key;
        }
        const auto was = v->vel;
        v->restart = v->key < 0;
        v->key = key;
        v->vel = velocity;
        v->pan = pan;
        v->cutSm = -1.0f;
        if (was <= 0.0f || v->released || P[singleVoice] == 0.0f)
        {
            v->released = false;
            v->ampRate = ampAttack;
            v->ampTarget = 5.0f;
            v->fenvRate = fenvAttack;
            v->fenvTarget = 5.0f;
            v->menvRate = menvAttack;
            v->menvTarget = 5.0f;
            v->egRate = egAttack;
            v->egTarget = 5.0f;
            v->eg = v->menv = v->fenv = 0.0f;
            v->restart = true;
            v->pitchOfs = pitch; // (FLM clears the three host offsets here; the host then sets the note's own)
            v->panOfs = v->velOfs = 0.0f;
        }
        lastNote = (float) key;
    }

    // the LFO for one chunk (one value per sample)
    void runLfo (int n, double songBeats, double tempo, bool playing) noexcept
    {
        auto a = lfoA;
        if (mwDest == 1)
        {
            const auto m = mwAmount * modWheel;
            a = 0.0f <= a ? a + m : a - m;
        }
        if (a <= -1.0f)
            a = -1.0f;
        if (1.0f <= a)
            a = 1.0f;
        switch (lfoDest)
        {
            case 0: lfoAmount = a * a * a; break;
            case 1: case 4: lfoAmount = std::fabs (a) * a; break;
            case 2: lfoAmount = std::fabs (a) * a * 12.0f; break;
            case 3: lfoAmount = a; break;
            default: break;
        }
        const auto seconds = (float) ((double) (lfoPeriod * 60.0f) / tempo);
        if (playing && 0.0f < lfoPeriod)
            lfoPhase = frac (songBeats / (double) lfoPeriod);
        const auto inc = seconds != 0.0f ? (float) (1.0 / ((double) seconds * fs)) : 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const auto ph = lfoPhase;
            float v = 0.0f;
            if (lfoShape == 0)
                v = (float) std::sin (ph * twoPi);
            else if (lfoShape == 1)
                v = (float) ((ph + ph) - 1.0);
            else if (lfoShape == 2)
                v = ph < 0.5 ? -1.0f : 1.0f;
            lfoPhase = frac ((double) inc + ph);
            lfoBuf[i] = v;
        }
    }

    // the voice's control values for one chunk; false = the voice gives no sound in it
    bool control (Voice& v, int n) noexcept
    {
        using namespace param;
        const auto fsf = (float) fs;
        {
            auto p = clamp01 (v.panOfs + v.pan);
            v.panL = v.panR = 1.0f;
            p = (p + p) - 1.0f;
            if (p <= -1.0f)
                p = -1.0f;
            if (1.0f <= p)
                p = 1.0f;
            if (0.0f < p)
                v.panL = 1.0f - p;
            else if (p < 0.0f)
                v.panR = p + 1.0f;
        }
        if (v.vel <= 0.0f)
            return false;
        v.velTarget = clamp01 (v.vel + v.velOfs);
        if (v.velSm < 0.0f)
            v.velSm = v.velTarget;
        // glide toward the key, once per chunk
        {
            const auto g = v.glide, t = (float) v.key;
            bool snap = true;
            if (0.0f <= g)
            {
                if (g == t)
                    snap = false;
                else
                {
                    const auto next = ((t - g) / (P[slideTime] * fsf * 0.1f + 1.0f)) * (float) n + g;
                    v.glide = next;
                    snap = t <= g ? ! (t <= next) : ! (next <= t);
                }
            }
            if (snap)
                v.glide = t;
        }
        auto pitch = bendSemis * bend + (v.pitchOfs + pitchOffset) + (float) i32 ((double) (P[transpose] * 48.0f - 24.0f)) + v.glide;
        if (egDest == 2)
            pitch = pitch + egAmount * v.eg;
        if (lfoDest == 2)
            pitch = pitch + lfoBuf[0] * lfoAmount;
        const auto f = powf32 (2.0f, (pitch - 57.0f) * 0.083333336f) * 220.0f;
        v.negRes = -clamp01 (P[resonance]);
        auto cut = (float) ((double) P[cutoff] + (double) (P[filterKeyboard] * (pitch - 64.0f)) * 0.008996365468350786);
        if (mwDest == 0)
            cut = mwAmount * modWheel + cut;
        v.cutTarget = cut < 0.0f ? 0.0f : (1.0f < cut ? 1.0f : cut);
        v.filterKind = std::clamp (i32 ((double) (P[filterKind] * 2.0f)), 0, 2);
        {
            const auto c = (P[filterContour] + P[filterContour]) - 1.0f;
            v.fenvAmount = c * c * c;
        }
        if (v.cutSm < 0.0f)
            v.cutSm = v.cutTarget;
        v.menvAmount = P[oscModContour] * 2.0f - 1.0f;
        v.modKind = i32 ((double) (P[oscModKind] * 3.0f));
        auto base = P[oscMod];
        if (mwDest == 2)
            base = mwAmount * modWheel + base;
        base = base < 0.0f ? 0.0f : (1.0f < base ? 1.0f : base);
        v.oscBase = base;
        if (v.modSm < 0.0f)
            v.modSm = base;
        const auto scale = i32 ((double) (P[choirScale] * 2.0f));
        const auto detune = P[choirDetune];
        v.tabA = tableFor (f, fs);
        v.tabB = tableFor (f * ratioB, fs);
        for (int u = 0; u < choir; ++u)
        {
            auto r = 1.0f;
            if (1 < choir)
            {
                r = powf32 (2.0f, (((float) u * detune) / (float) (choir - 1) - detune * 0.5f) * 0.083333336f);
                if (scale == 1)
                {
                    if (u & 1)
                        r = r * 1.5f;
                }
                else if (scale == 2 && (u & 1))
                    r = r + r;
            }
            auto fA = r * f;
            const auto fB = fA * ratioB;
            if (v.modKind == 2) // SNC: osc A runs 2^M above its pitch (and is reset by osc B's wrap)
            {
                auto m = v.menv * v.menvAmount + v.oscBase;
                if (egDest == 1)
                    m = m + egAmount * v.eg;
                if (lfoDest == 1)
                    m = m + lfoAmount * lfoBuf[0];
                fA = fA * powf32 (2.0f, m);
            }
            if (v.restart) // osc B starts at osc A's phase scaled to its own frequency + Osc B Phase
            {
                const auto a = (double) v.phA[u] * 2.3283064370807974e-10;
                const auto ph = (double) (fB / fA) * a + (double) P[oscBPhase];
                v.phB[u] = u32of (frac (ph) * 4294967295.0);
            }
            v.incA[u] = u32of (((double) fA / fs) * 4294967295.0);
            v.incB[u] = u32of (((double) fB / fs) * 4294967295.0);
        }
        v.restart = false;
        v.silent = ! (1.0e-4f <= v.velSm || v.velTarget != 0.0f);
        return ! v.silent;
    }

    void renderVoice (Voice& v, int n) noexcept
    {
        auto egAmt = egAmount;
        if (P[param::egVelocity] != 0.0f)
            egAmt = egAmt * clamp01 (v.velOfs + v.vel);
        if (v.cutSm < 0.0f)
            v.cutSm = v.cutTarget;
        const auto* ta = tabA->data() + (std::size_t) v.tabA * tableSize;
        const auto* tb = tabB->data() + (std::size_t) v.tabB * tableSize;
        const auto* G = filter->g.data();
        const auto* K = filter->k.data();
        float modIn = 0.0f, lastL = 0.0f, lastR = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            float outL = 0.0f, outR = 0.0f;
            if (! v.silent)
            {
                auto M = v.menv * v.menvAmount + v.modSm;
                if (egDest == 1)
                    M = M + v.eg * egAmt;
                if (lfoDest == 1)
                    M = M + lfoAmount * lfoBuf[i];
                if (1.0f <= M)
                    M = 1.0f;
                if (M <= 0.0f)
                    M = 0.0f;
                const auto active = 0.001f < std::fabs (M);
                rng = rng * 0x0bb38435u + 0x3619636bu;
                const auto dither = rng >> 10;
                float aL = 0.0f, aR = 0.0f, bL = 0.0f, bR = 0.0f, b0 = 0.0f;
                const auto kind = v.modKind;
                for (int u = 0; u < choir; ++u)
                {
                    const auto b = tb[(v.phB[u] + dither) >> 22];
                    float a;
                    if (kind == 2)
                    {
                        a = ta[(v.phA[u] + dither) >> 22];
                        const auto was = v.phB[u];
                        v.phB[u] = was + v.incB[u];
                        if (v.phB[u] < was)
                            v.phA[u] = 0;
                        v.phA[u] += v.incA[u];
                    }
                    else
                    {
                        if (active && kind == 0)
                            a = (b * M * 3.0f + 1.0f) * ta[(v.phA[u] + dither) >> 22];
                        else if (active && kind == 1)
                            a = ta[(u32of ((double) ((b + 1.0f) * M * 16777215.0f)) * 0x100u + v.phA[u] + dither) >> 22];
                        else
                            a = ta[(v.phA[u] + dither) >> 22];
                        if (active && kind == 3 && u == 0)
                            b0 = b;
                        v.phA[u] += v.incA[u];
                        v.phB[u] += v.incB[u];
                    }
                    aL = aL + a * mix[u][0];
                    aR = aR + a * mix[u][1];
                    bL = bL + b * mix[u][2];
                    bR = bR + b * mix[u][3];
                }
                auto L = aL + bL, R = aR + bR;
                if (noise != 0.0f)
                {
                    rng = rng * 0x0bb38435u + 0x3619636bu;
                    const auto nz = ((float) rng * 4.656613e-10f - 1.0f) * noise;
                    L = L + nz;
                    R = R + nz;
                }
                // the filter's (g, k) pair at the cutoff of this sample
                auto c = M * b0 * 0.5f + v.cutSm + v.fenv * v.fenvAmount;
                if (egDest == 0)
                    c = c + v.eg * egAmt;
                if (lfoDest == 0)
                    c = c + lfoAmount * lfoBuf[i];
                auto d = (double) c * 65534.99;
                if (d <= 0.0)
                    d = 0.0;
                if (65534.99 <= d)
                    d = 65534.99;
                const auto at = (int) d;
                const auto fr = (float) (d - (double) at);
                const auto g = (1.0f - fr) * G[at] + fr * G[at + 1];
                const auto k = ((1.0f - fr) * K[at] + fr * K[at + 1]) * v.negRes;
                L = L + v.y[7][0] * k;
                R = R + v.y[7][1] * k;
                const auto sL = L / (std::fabs (L) * 0.05f + 1.0f), sR = R / (std::fabs (R) * 0.05f + 1.0f);
                v.y[0][0] = (sL - v.y[0][0]) * g + v.y[0][0];
                v.y[0][1] = (sR - v.y[0][1]) * g + v.y[0][1];
                v.fenv = (v.fenvTarget - v.fenv) * v.fenvRate + v.fenv;
                v.amp = (v.ampTarget - v.amp) * v.ampRate + v.amp;
                for (int s = 1; s < 8; ++s)
                {
                    v.y[s][0] = (v.y[s - 1][0] - v.y[s][0]) * g + v.y[s][0];
                    v.y[s][1] = (v.y[s - 1][1] - v.y[s][1]) * g + v.y[s][1];
                }
                v.eg = (v.egTarget - v.eg) * v.egRate + v.eg;
                v.menv = (v.menvTarget - v.menv) * v.menvRate + v.menv;
                v.velSm = (v.velTarget - v.velSm) * 0.01f + v.velSm;
                v.cutSm = (v.cutTarget - v.cutSm) * 0.001f + v.cutSm;
                // the Osc Mod knob's smoother starts each sample from the knob and leans 1 % toward the velocity
                modIn = (v.velSm - v.oscBase) * 0.01f + v.oscBase;
                v.modSm = (modIn - v.modSm) * 0.01f + v.modSm;
                if (v.filterKind == 0)
                    outL = v.y[3][0], outR = v.y[3][1];
                else if (v.filterKind == 1)
                    outL = v.y[7][0], outR = v.y[7][1];
                else
                    outL = sL - v.y[0][0], outR = sR - v.y[0][1];
            }
            else
            {
                for (int u = 0; u < choir; ++u)
                {
                    v.phA[u] += v.incA[u];
                    v.phB[u] += v.incB[u];
                }
                v.fenv = v.fenv + (v.fenvTarget - v.fenv) * v.fenvRate;
                v.eg = v.eg + (v.egTarget - v.eg) * v.egRate;
                v.amp = v.amp + (v.ampTarget - v.amp) * v.ampRate;
                v.menv = v.menv + (v.menvTarget - v.menv) * v.menvRate;
            }
            if (1.0f < v.fenv)
            {
                v.fenvTarget = 0.0f;
                v.fenv = 1.0f;
                v.fenvRate = fenvDecay;
            }
            if (1.0f < v.menv)
            {
                v.menvTarget = 0.0f;
                v.menv = 1.0f;
                v.menvRate = menvDecay;
            }
            if (1.0f < v.eg)
            {
                v.eg = 1.0f;
                v.egTarget = egSustain;
                v.egRate = egDecay;
            }
            if (1.0f < v.amp)
            {
                v.ampTarget = ampSustain;
                v.ampRate = ampDecay;
                v.amp = 1.0f;
            }
            lastL = v.amp * outL * v.velSm;
            lastR = v.amp * outR * v.velSm;
            bufL[i] = lastL * v.panL + bufL[i];
            bufR[i] = lastR * v.panR + bufR[i];
        }
        (void) modIn;
        if (v.amp < 1.0e-4f && v.ampTarget == 0.0f)
            resetVoice (v);
    }

    // one chunk of at most 32 samples, added to the host buffers
    void chunk (float* outL, float* outR, int n, double songBeats, double tempo, bool playing) noexcept
    {
        using namespace param;
        bool any = false;
        std::fill (bufL, bufL + n, 0.0f);
        std::fill (bufR, bufR + n, 0.0f);
        if (started && tabA != nullptr && tabB != nullptr && filter != nullptr)
        {
            const auto e = egContour;
            egAmount = egDest == 0 ? e * e * e : (egDest == 1 ? std::fabs (e) * e : (egDest == 2 ? std::fabs (e) * e * 12.0f : egAmount));
            runLfo (n, songBeats, tempo, playing);
            const auto m = P[oscBMix] * 2.0f - 1.0f;
            const auto nz = P[param::noise];
            noise = nz * nz;
            const auto rest = 1.0f - nz * nz;
            const auto gB = rest * m, gA = (1.0f - std::fabs (m)) * rest;
            choir = std::clamp (i32 ((double) (P[choirVoices] * 3.0f)), 0, 3) + 1;
            const auto spread = P[choirStereo];
            const auto norm = choir < 2 ? 0.5f : 0.5f / ((float) (choir - 1) * 0.15f + 1.0f);
            for (int u = 0; u < choir; ++u)
            {
                auto s = 0.0f;
                if (1 < choir)
                {
                    s = (float) u / (float) (choir - 1);
                    s = (s + s) - 1.0f;
                }
                const auto l = clamp01 (1.0f - s * spread), r = clamp01 (s * spread + 1.0f);
                mix[u][0] = l * gA * norm;
                mix[u][1] = r * gA * norm;
                mix[u][2] = l * gB * norm;
                mix[u][3] = r * gB * norm;
            }
            for (auto& v : voices)
                if (0.0f < v.vel)
                {
                    any = control (v, n) || any;
                    renderVoice (v, n);
                }
        }
        for (auto& f : fxs)
        {
            if (! (f.running()))
                continue;
            if (playing && 0.0f < f.period)
                f.phase = frac ((double) (float) (songBeats * 4.0) * (0.25 / (double) f.period));
            any = f.process (bufL, bufR, n, (float) tempo, any) || any;
        }
        lastOut = any;
        if (! any)
        {
            gainL = gainR = -1.0f;
            return;
        }
        auto db = P[level] * 66.0f - 48.0f;
        if (lfoDest == 3)
            db = db + lfoBuf[0] * 12.0f * lfoAmount;
        const auto gain = -60.0f <= db ? powf32 (10.0f, db * 0.05f) : 0.0f;
        auto q = P[param::pan];
        if (lfoDest == 4)
            q = q + lfoBuf[0] * lfoAmount;
        q = clamp01 (q);
        auto r = q + q, l = 2.0f - r;
        if (1.0f <= r)
            r = 1.0f;
        if (1.0f <= l)
            l = 1.0f;
        const auto targetR = r * gain * r, targetL = l * gain * l;
        if (gainL < 0.0f)
            gainL = targetL;
        if (gainR < 0.0f)
            gainR = targetR;
        for (int i = 0; i < n; ++i)
        {
            outL[i] = bufL[i] * gainL + outL[i];
            outR[i] = bufR[i] * gainR + outR[i];
            gainL = (targetL - gainL) * 0.01f + gainL;
            gainR = (targetR - gainR) * 0.01f + gainR;
        }
    }
};

// ------------------------------------------------------------------------------------------------------- public

Player::Player() : impl (std::make_unique<Impl>())
{
    impl->patch = defaultPatch();
    impl->P = impl->patch.P;
    impl->reset();
    impl->applyAll();
}
Player::~Player() = default;

void Player::prepare (double sampleRate)
{
    impl->fs = sampleRate > 0.0 ? sampleRate : 44100.0;
    impl->reset();
    impl->applyAll();
    for (auto& f : impl->fxs)
        f.ensureReverb();
}
double Player::sampleRate() const noexcept { return impl->fs; }

void Player::setPatch (const Patch& patch, const std::string& resourceRoot)
{
    impl->patch = patch;
    impl->P = patch.P;
    impl->tabA = waveTables (resourceRoot, patch.waveA);
    impl->tabB = waveTables (resourceRoot, patch.waveB);
    impl->filter = filterTable (resourceRoot);
    impl->applyAll();
    for (auto& f : impl->fxs)
        f.ensureReverb();
}
bool Player::resourcesLoaded() const noexcept { return impl->tabA != nullptr && impl->tabB != nullptr && impl->filter != nullptr; }
void Player::setParam (int index, float value) noexcept { impl->setParam (index, value); }
void Player::note (int key, float velocity, float pan, float pitch) noexcept { impl->note (key, velocity, pan, pitch); }
void Player::setPitchOffset (float semitones) noexcept { impl->pitchOffset = semitones; }
void Player::setModWheel (float value) noexcept { impl->modWheel = std::isfinite (value) ? std::clamp (value, 0.0f, 1.0f) : 0.0f; } // round 190dw item 4
void Player::allNotesOff() noexcept
{
    for (int k = 0; k < 128; ++k)
        if (impl->keyVel[k] != 0.0f)
            impl->note (k, 0.0f, 0.5f, 0.0f);
}

void Player::render (float* left, float* right, int n, double tempo, double songBeats) noexcept
{
    if (impl->P[param::enable] < 0.5f)
        return;
    const auto playing = songBeats >= 0.0;
    auto pos = playing ? songBeats : 0.0;
    for (int at = 0; at < n;)
    {
        const auto len = std::min (chunkSize, n - at);
        impl->chunk (left + at, right + at, len, pos, tempo, playing);
        at += len;
        pos = pos + (double) len * tempo * (0.016666666666666666 / impl->fs);
    }
}

bool Player::idle() const noexcept { return ! impl->lastOut && activeVoices() == 0; }
int Player::activeVoices() const noexcept
{
    int n = 0;
    for (const auto& v : impl->voices)
        n += 0.0f < v.vel ? 1 : 0;
    return n;
}

namespace fx
{
bool implemented (int kind) noexcept { return kind >= dist && kind <= vox; } // (all ten since round 190dw)
const char* name (int kind) noexcept
{
    static const char* const names[10] = { "DIST", "FLNG", "PHAS", "F LP", "F HP", "DLY", "RVRB", "PAN", "RING", "VOX" };
    return kind >= 0 && kind < 10 ? names[kind] : "";
}
} // namespace fx

int fxKindOf (float v) noexcept { return std::clamp (i32 ((double) (v * 9.0f)), 0, 9); }

float attackRate (float v, double fs) noexcept
{
    if (v == 0.0f)
        return 1.0f;
    return (float) (8.82 / ((double) (v * v * v * v * 100.0f) * fs));
}
float decayRate (float v, double fs) noexcept
{
    const auto c = v * v * v;
    return (float) (8.82 / (((double) (c * c * v * 200.0f) + 0.001) * fs));
}
float ampReleaseRate (float v, double fs) noexcept { return (float) ((8.82 / fs) / ((double) v + 0.001)); }
float egReleaseRate (float v, double fs) noexcept { return (float) (8.82 / (((double) (v * v * v * v * 200.0f) + 0.001) * fs)); }

int filterFileIndex (int i) noexcept
{
    const auto f = std::pow (2.0, (double) i * 0.00014145113298237583 + 5.0);
    return std::clamp (i32 ((f - 11.0) * 3.336982534752279), 0, 65535);
}

int tableFor (float f, double fs) noexcept
{
    const auto fsf = (float) fs;
    if (fsf * 0.0009765625f < f)
        return std::clamp (i32 ((double) ((0.5f / f) * fsf)), 0, 511);
    return 511;
}

Patch defaultPatch()
{
    using namespace param;
    Patch p;
    p.P[enable] = 1.0f;
    p.P[level] = 0.72727275f;
    p.P[param::pan] = 0.5f;
    p.P[ampDecay] = p.P[ampSustain] = 1.0f;
    p.P[ampRelease] = 0.2f;
    for (const auto k : { 7, 8, 9, 10, oscBPitch, oscBDetune, oscBMix, oscModContour, filterContour, transpose, lfoAmount, egContour })
        p.P[(std::size_t) k] = 0.5f;
    p.P[oscModDecay] = p.P[cutoff] = p.P[filterDecay] = p.P[lfoRate] = 1.0f;
    p.P[egDecay] = p.P[egSustain] = p.P[egRelease] = 1.0f;
    const float kinds[3] = { 0.11111111f, 0.5555556f, 0.33333334f };
    for (int s = 0; s < 3; ++s)
    {
        const auto b = (std::size_t) (fxBase + fxStride * s);
        p.P[b + fxKind] = kinds[s];
        p.P[b + fxAmount] = p.P[b + fxX] = p.P[b + fxY] = 0.5f;
    }
    return p;
}

std::string waveFileName (int index) { return index >= 0 && index < waveCount ? waveNames[index] : ""; }

std::vector<std::uint8_t> writeState (const Patch& patch)
{
    std::vector<std::uint8_t> s { 'F', 'L', 'M', 'G', 1, 0, 0, 0 };
    const auto put = [&s] (const void* p) {
        std::uint8_t b[4];
        std::memcpy (b, p, 4);
        s.insert (s.end(), b, b + 4);
    };
    for (const auto v : patch.P)
        put (&v);
    const std::int32_t a = patch.waveA, b = patch.waveB;
    put (&a);
    put (&b);
    put (&patch.bendRange);
    put (&patch.mwAmount);
    put (&patch.mwDest);
    return s;
}

std::optional<Patch> readState (const std::vector<std::uint8_t>& s)
{
    constexpr std::size_t size = 8 + 4 * paramCount + 20;
    if (s.size() < size || std::memcmp (s.data(), "FLMG", 4) != 0 || s[4] != 1 || s[5] != 0 || s[6] != 0 || s[7] != 0)
        return std::nullopt;
    Patch p;
    for (int i = 0; i < paramCount; ++i)
    {
        float v;
        std::memcpy (&v, s.data() + 8 + 4 * (std::size_t) i, 4);
        p.P[(std::size_t) i] = std::isfinite (v) ? v : 0.0f;
    }
    const auto* c = s.data() + 8 + 4 * paramCount;
    std::int32_t a, b;
    std::memcpy (&a, c, 4);
    std::memcpy (&b, c + 4, 4);
    std::memcpy (&p.bendRange, c + 8, 4);
    std::memcpy (&p.mwAmount, c + 12, 4);
    std::memcpy (&p.mwDest, c + 16, 4);
    if (a < 0 || a >= waveCount || b < 0 || b >= waveCount || ! std::isfinite (p.bendRange) || ! std::isfinite (p.mwAmount) || ! std::isfinite (p.mwDest))
        return std::nullopt;
    p.waveA = a;
    p.waveB = b;
    return p;
}

std::vector<float> renderReference (const Patch& patch, const std::vector<RefNote>& notes, double sampleRate, int length,
                                    const std::string& resourceRoot, int block, double bpm, double songPos)
{
    Player player;
    player.prepare (sampleRate);
    player.setPatch (patch, resourceRoot);
    if (! player.resourcesLoaded() || length <= 0)
        return {};
    std::vector<int> events;
    for (const auto& n : notes)
    {
        events.push_back (n.start);
        if (n.release >= 0)
            events.push_back (n.release);
    }
    block = std::max (1, block);
    std::vector<float> out (2 * (std::size_t) length, 0.0f), l ((std::size_t) block), r ((std::size_t) block);
    for (int at = 0; at < length;)
    {
        for (const auto& n : notes)
        {
            if (n.release == at)
                player.note (n.key, 0.0f);
            if (n.start == at)
                player.note (n.key, n.velocity, n.pan);
        }
        auto len = std::min (block, length - at);
        for (const auto e : events)
            if (at < e && e < at + len)
                len = e - at;
        std::fill (l.begin(), l.begin() + len, 0.0f);
        std::fill (r.begin(), r.begin() + len, 0.0f);
        player.render (l.data(), r.data(), len, bpm, songPos >= 0.0 ? songPos + (double) at * bpm / (60.0 * sampleRate) : -1.0);
        for (int i = 0; i < len; ++i)
        {
            out[2 * (std::size_t) (at + i)] = l[(std::size_t) i];
            out[2 * (std::size_t) (at + i) + 1] = r[(std::size_t) i];
        }
        at += len;
    }
    return out;
}
} // namespace gmsmobile

bool gmsIsFlm (const ChannelDef& channel) noexcept
{
    return channel.instrument == InstrumentKind::gms && ! channel.modelParams.empty() && channel.modelParams[0] >= 0.5f;
}
} // namespace lynx
