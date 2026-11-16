#pragma once

// FL Studio's Pitcher DSP, rebuilt from the behaviour decoded in docs/analise/REA_rodada30.md §2 (clean-room; the
// reference model r30/py/pitcher_model.py matches the installation's engine bit-exactly in 16 of 21 cases, the rest
// limited by the CRT's log2f rounding). Header-only so the numerical harness can drive it directly.
//
// Detector (§2.2): a 0.6 tracker high-pass, 16 one-pole low-passes in double, an integer-lag AMDF over frames of
// N = 4·⌊fs/f_min/4⌋ samples, first local minimum below 0.3, valid f_min…1000 Hz. Correction (§2.3): nearest
// allowed note of a 128-note mask (midpoint rule), key bypass, Correction Speed pole, Pitch Reference.
// Shifters: plain (§2.4: 4 Hermite read heads on a 2N delay line, period splices with a 128-sample cross-fade,
// 9-sample latency) or formant (§2.5: TD-PSOLA with 2-period Hann grains resampled by the Gender factor; a
// 256-Hann overlap-add with 257-sample latency while unvoiced or unshifted). Harmonize and MIDI (§2.6/2.7) take
// held notes through noteOn()/noteOff().
//
// Arithmetic follows the model: float32 everywhere, double where the model uses it (filter stages, log2, pow,
// cos). Memory is reserved by reserve(); nothing allocates while processing.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

// The model rounds every float32 operation: no fused multiply-adds here (on FMA targets they move the output by
// ~1e-7 and, over time, the instant of a splice). Round 190ie: clang saves the includer's floating-point state and
// gives it back at the end of the header (float_control push / pop); it used to end on "contract(on)", which left
// fused multiply-adds on for the rest of core/Effects.cpp under clang (not under GCC, which pops its options)
#if defined(__clang__)
#pragma float_control(push)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC push_options
#pragma GCC optimize("fp-contract=off")
#endif

namespace lynx::pitcher
{
// FL's raw parameter values (Pitcher parameter indices in brackets)
struct Params
{
    float speed = 1.0f;     // [0] Correction Speed
    float stereo = 1.0f;    // [1] harmony voice pan multiplier
    float gender = 0.5f;    // [2] formant path: grain resampling 2^(2v − 1)
    float fine = 0.5f;      // [3] 2v − 1 semitones
    bool bypassAll = false; // [4]
    std::array<bool, 12> keyBypass {}; // [5…16]
    std::array<float, 4> voicePan { 0.5f, 0.5f, 0.5f, 0.5f }; // [17…20]
    bool formant = false;   // [22]
    float aRef = 0.5f;      // [23] A = 55·2^((2v − 1)/12)
    float pitchRef = 0.0f;  // [24] int(2v): 0 left, 1 independent, 2 right
    bool midi = false, octaves = false, harmonize = false; // [25] [26] [27]
    bool mix = false, vel = false;                          // [30] Replace/Mix, [31] velocity weight
    int range = 2;                                          // [32] f_min 220/170/110/80/55/25 Hz
    std::array<bool, 12> scale { true, true, true, true, true, true, true, true, true, true, true, true }; // note mask C…B
};

class Pitcher
{
public:
    static constexpr double rangeHz[6] { 220.0, 170.0, 110.0, 80.0, 55.0, 25.0 };

    static int windowFor (double fs, int range) noexcept
    {
        return 4 * ((int) (fs / rangeHz[range < 0 ? 0 : (range > 5 ? 5 : range)]) >> 2);
    }

    // Reserves the buffers for every range up to `maxFs` (call off the audio thread).
    void reserve (double maxFs)
    {
        const auto n = (std::size_t) windowFor (maxFs, 5) + 4;
        const auto b = (std::size_t) (maxFs * 0.1) + 4;
        for (int c = 0; c < 2; ++c)
        {
            hist[c].assign (n, 0.0f);
            amdf[c].assign (n, 0.0f);
            delay[c].assign (2 * n, 0.0f);
            inRing[c].assign (b + 2, 0.0f);
            lpRing[c].assign (b + 2, 0.0f);
            outRing[c].assign (b, 0.0f);
        }
        grain.assign (2 * n + 4, 0.0f);
        capacityN = (int) n - 4;
        capacityB = (int) b - 4;
        for (int i = 0; i < 256; ++i)
            hann256[(std::size_t) i] = (float) ((1.0 - std::cos ((double) ((float) i * 0.00390625f) * 6.283185307179586)) * 0.5);
    }

    void prepare (double sampleRate) noexcept
    {
        fs = sampleRate;
        initialised = false;
    }

    void reset() noexcept { initialised = false; }

    // MIDI input (Harmonize / MIDI mode): velocity 0…127, 0 = note off
    void noteOn (int key, int velocity) noexcept
    {
        if (key >= 0 && key < 128)
            held[(std::size_t) key] = (float) velocity * 0.007874015718698502f;
    }
    void noteOff (int key) noexcept { noteOn (key, 0); }
    void allNotesOff() noexcept { held.fill (0.0f); }

    // Latency of the current path in samples (plain 9; formant pass-through 257)
    int latency() const noexcept { return formantOn ? 257 : 9; }

    void process (float* left, float* right, int n, const Params& p) noexcept
    {
        configure (p);
        if (N <= 0)
            return;
        harmony (p);
        const auto L = this->L;
        for (int t = 0; t < n; ++t)
        {
            const float in[2] { left[t], right[t] };
            float shifts[2][4] {}, gains[2][4] {};
            bool mixDry = false;
            if (! p.harmonize)
            {
                shifts[0][0] = fine + (ref == 2 ? sm[1] : sm[0]);
                shifts[1][0] = fine + ((ref == 1 || ref == 2) ? sm[1] : sm[0]);
                gains[0][0] = gains[1][0] = 1.0f;
            }
            else
            {
                for (int c = 0; c < 2; ++c)
                    for (int v = 0; v < 4; ++v)
                    {
                        shifts[c][v] = hShift[c][v];
                        gains[c][v] = hGain[c][v];
                    }
                mixDry = hMix;
            }

            float o[2] { 0.0f, 0.0f };
            if (formantOn)
            {
                if (ref == 0)
                    std::copy (timer[0], timer[0] + 4, timer[1]);
                else if (ref == 2)
                    std::copy (timer[1], timer[1] + 4, timer[0]);
                o[0] = renderFormant (0, shifts, gains, in[0]);
                o[1] = renderFormant (1, shifts, gains, in[1]);
                if (++W2 == B)
                    W2 = 0;
            }
            // the reference channel's read heads are copied (only the heads, as FL does)
            if (ref == 0)
                std::copy (R[0], R[0] + 4, R[1]);
            else if (ref == 2)
                std::copy (R[1], R[1] + 4, R[0]);
            if (! formantOn)
            {
                for (int c = 0; c < 2; ++c)
                {
                    const auto P = period[c];
                    auto* dl = delay[c].data();
                    for (int v = 0; v < 4; ++v)
                    {
                        auto rho = 1.0f;
                        if (P != 0.0f)
                        {
                            auto s = shifts[c][v];
                            s = s > 12.0f ? 12.0f : (s < -12.0f ? -12.0f : s);
                            rho = pow2Cached (rhoCache[c][v], s * 0.083333336f);
                        }
                        auto r = R[c][v];
                        auto y = interp (dl, L, r);
                        if (rho < 1.0f)
                        {
                            auto d = r - (float) W;
                            if (d < 0.0f)
                                d = d + (float) L;
                            if (d <= 128.0f)
                            {
                                O[c][v] = r;
                                r = r + P;
                                K[c][v] = 128;
                            }
                        }
                        else if (rho > 1.0f)
                        {
                            auto d = r - (float) W;
                            if (d < 0.0f)
                                d = d + (float) L;
                            if (d >= (float) (L - 128))
                            {
                                O[c][v] = r;
                                r = r - P;
                                K[c][v] = 128;
                                if (r < 0.0f)
                                    r = (float) L + r;
                            }
                        }
                        const auto k = K[c][v];
                        if (k > 0)
                        {
                            const auto yo = interp (dl, L, O[c][v]);
                            K[c][v] = k - 1;
                            O[c][v] = O[c][v] + rho;
                            const auto w = (float) k * 0.0078125f;
                            y = (1.0f - w) * y + w * yo;
                        }
                        while (r >= (float) L)
                            r = r - (float) L;
                        while (r < 0.0f)
                            r = r + (float) L;
                        R[c][v] = r + rho;
                        const auto g = (gains[c][v] - G[c][v]) * 0.007f + G[c][v];
                        G[c][v] = g;
                        o[c] = o[c] + g * y;
                    }
                    dl[W] = in[c];
                }
                if (++W == L)
                    W = 0;
            }
            if (mixDry)
            {
                o[0] = o[0] + in[0];
                o[1] = o[1] + in[1];
            }
            left[t] = o[0];
            right[t] = o[1];

            // detector
            for (int c = 0; c < 2; ++c)
            {
                const auto xi = in[c];
                const auto d = (xi - dc[c]) * 0.6f + dc[c];
                dc[c] = d;
                if (1e-9f > d && d > -1e-9f)
                    dc[c] = 0.0f;
                auto v = (double) (xi - d);
                auto& st = lp[c];
                for (int k = 0; k < 16; ++k)
                {
                    const auto s = (v - st[(std::size_t) k]) * 0.125 + st[(std::size_t) k];
                    st[(std::size_t) k] = (1e-9 > s && s > -1e-9) ? 0.0 : s;
                    v = (double) (float) s;
                }
                const auto y = (float) v;
                lpRing[c][(std::size_t) W2] = y;
                // AMDF: slot k (lag N − k) pairs y with the k-th oldest history value
                auto* A = amdf[c].data();
                const auto* H = hist[c].data();
                const auto h0 = histPos;
                // two contiguous runs of the ring (no wrap test inside): each A[k] still gets the same single
                // addition, so the result is unchanged, and the loops vectorise
                const auto first = N - h0;
                for (int k = 0; k < first; ++k)
                    A[k] += std::abs (H[h0 + k] - y);
                for (int k = first; k < N; ++k)
                    A[k] += std::abs (H[k - first] - y);
                hist[c][(std::size_t) h0] = y;
                sm[c] = freq[c] == 0.0f ? 0.0f : (target[c] - sm[c]) * speed + sm[c];
            }
            if (++histPos == N)
                histPos = 0;
            if (++frameCount == N)
            {
                detect();
                frameCount = 0;
            }
        }
    }

private:
    void configure (const Params& p) noexcept
    {
        const auto range = p.range < 0 ? 0 : (p.range > 5 ? 5 : p.range);
        auto n = windowFor (fs, range);
        if (n > capacityN)
            n = capacityN;
        if (! initialised || n != N || range != rangeIndex)
        {
            rangeIndex = range;
            N = n;
            L = 2 * n;
            B = (int) (fs * 0.1);
            if (B > capacityB)
                B = capacityB;
            fmin = rangeHz[range];
            restartDetector();
            formantOn = p.formant;
            restartShifter();
            initialised = true;
        }
        else if (p.formant != formantOn)
        {
            formantOn = p.formant;
            restartShifter();
        }

        const auto u = 1.0f - p.speed;
        speed = (double) u >= 0.01 ? (float) (8.82 / (fs * (double) (u * u * u * 3.0f + 0.001f))) : 1.0f;
        fine = p.fine + p.fine - 1.0f;
        A = (float) (std::pow (2.0, (2.0 * (double) p.aRef - 1.0) / 12.0) * 55.0);
        ref = (int) ((double) p.pitchRef + (double) p.pitchRef);
        if (p.gender != lastGenderParam)
        {
            lastGenderParam = p.gender;
            genderStep = (float) std::pow (2.0, (double) (2.0f * p.gender - 1.0f));
        }
        midi = p.midi;
        bypassAll = p.bypassAll;
        keyBypass = p.keyBypass;
        buildMask (p);
    }

    void restartDetector() noexcept
    {
        for (int c = 0; c < 2; ++c)
        {
            std::fill (hist[c].begin(), hist[c].end(), 0.0f);
            std::fill (amdf[c].begin(), amdf[c].end(), 0.0f);
            lp[c].fill (0.0);
            dc[c] = 0.0f;
            freq[c] = period[c] = cls[c] = target[c] = sm[c] = 0.0f;
            cls[c] = -1.0f;
            octave[c] = -1;
        }
        histPos = frameCount = 0;
    }

    // FL's restart (Formant toggled, sample rate set): buffers cleared, heads at L − 10, timers and gains 0
    void restartShifter() noexcept
    {
        for (int c = 0; c < 2; ++c)
        {
            std::fill (delay[c].begin(), delay[c].end(), 0.0f);
            std::fill (inRing[c].begin(), inRing[c].end(), 0.0f);
            std::fill (lpRing[c].begin(), lpRing[c].end(), 0.0f);
            std::fill (outRing[c].begin(), outRing[c].end(), 0.0f);
            for (int v = 0; v < 4; ++v)
            {
                R[c][v] = (float) L - 10.0f;
                O[c][v] = 0.0f;
                K[c][v] = 0;
                G[c][v] = 0.0f;
                timer[c][v] = 0;
            }
        }
        W = W2 = 0;
    }

    void buildMask (const Params& p) noexcept
    {
        mask.fill (false);
        if (! p.midi)
        {
            for (int c = 0; c < 12; ++c)
                if (p.scale[(std::size_t) c])
                    for (int k = c; k < 128; k += 12)
                        mask[(std::size_t) k] = true;
        }
        else if (! p.octaves)
        {
            for (int k = 0; k < 128; ++k)
                if (held[(std::size_t) k] != 0.0f)
                    for (int j = k % 12; j < 128; j += 12)
                        mask[(std::size_t) j] = true;
        }
        else
        {
            for (int k = 13; k < 128; ++k)
                if (held[(std::size_t) k] != 0.0f)
                    mask[(std::size_t) (k - 12)] = true;
        }
        anyAllowed = false;
        for (auto m : mask)
            anyAllowed = anyAllowed || m;
    }

    // frame end: normalised AMDF, first local minimum below 0.3 from the short lags
    void detect() noexcept
    {
        for (int c = 0; c < 2; ++c)
        {
            auto* a = amdf[c].data();
            auto mx = 0.0f;
            for (int i = 0; i < N; ++i)
                mx = a[i] > mx ? a[i] : mx;
            if (mx == 0.0f)
            {
                period[c] = freq[c] = 0.0f;
                continue;
            }
            const auto inv = 1.0f / mx;
            for (int i = 0; i < N; ++i)
                a[i] = inv * a[i];
            auto found = -1;
            for (int i = N - 2; i > 0; --i)
                if (a[i] < a[i - 1] && a[i] < a[i + 1] && a[i] < 0.3f)
                {
                    found = i;
                    break;
                }
            auto f = 0.0f;
            if (found < 0)
                period[c] = 0.0f;
            else
            {
                period[c] = (float) N - (float) found;
                f = (float) (fs / (double) period[c]);
            }
            freq[c] = f;
            if (f < (float) fmin || 1000.0f < f)
                period[c] = freq[c] = 0.0f;
        }
        for (int c = 0; c < 2; ++c)
            std::fill (amdf[c].begin(), amdf[c].begin() + N, 0.0f);
        targets();
    }

    void targets() noexcept
    {
        for (int c = 0; c < 2; ++c)
        {
            if (freq[c] == 0.0f || ! anyAllowed)
            {
                cls[c] = -1.0f;
                octave[c] = -1;
                target[c] = sm[c] = 0.0f;
                continue;
            }
            const auto lg = (float) std::log2 ((double) (A / freq[c]));
            const auto nn = 9.0f - lg * 12.0f;
            const auto o = (int) (nn * 0.083333336f);
            octave[c] = o;
            auto idx = (int) std::abs (nn + 0.5f);
            idx = idx > 127 ? 127 : idx;
            const auto cl = nn - (float) o * 12.0f;
            cls[c] = cl;
            auto lo = idx, hi = idx;
            if (! mask[(std::size_t) idx])
            {
                while (lo >= 1)
                    if (mask[(std::size_t) --lo])
                        break;
                while (hi <= 0x7e)
                    if (mask[(std::size_t) ++hi])
                        break;
            }
            const auto chosen = nn < (float) (hi - lo) * 0.5f + (float) lo ? lo : hi;
            target[c] = (float) chosen - nn;
            if (! midi)
            {
                auto k = (int) (cl + 0.5f);
                if (k >= 12)
                    k = 0;
                if (k < 0)
                    k += 12; // (the model's index wraps; only reachable below C1 with the 25 Hz range)
                if (keyBypass[(std::size_t) k] || bypassAll)
                    target[c] = 0.0f;
            }
        }
    }

    // Harmonize: voices from the first 4 held notes, once per block
    void harmony (const Params& p) noexcept
    {
        if (! p.harmonize)
            return;
        for (int c = 0; c < 2; ++c)
            for (int v = 0; v < 4; ++v)
                hGain[c][v] = hShift[c][v] = 0.0f;
        hMix = p.mix;
        if (! hMix)
            hGain[0][0] = hGain[1][0] = 1.0f;
        int notes[4] { -1, -1, -1, -1 };
        float weights[4] {};
        if (p.midi)
        {
            int count = 0;
            for (int k = 0; k < 128 && count < 4; ++k)
            {
                const auto v = held[(std::size_t) k];
                if (v == 0.0f)
                    continue;
                if (! p.octaves)
                {
                    const auto cl = k % 12;
                    if (cl == notes[0] || cl == notes[1] || cl == notes[2])
                        continue;
                    weights[count] = v * v + v * v;
                    notes[count] = cl;
                }
                else
                    notes[count] = k - 12;
                ++count;
            }
        }
        for (int c = 0; c < 2; ++c)
        {
            auto nd = cls[c];
            if (p.octaves)
                nd = (float) ((double) octave[c] * 12.0 + (double) nd);
            for (int j = 0; j < 4; ++j)
                if (notes[j] >= 0 && freq[c] != 0.0f)
                {
                    const auto d = (p.voicePan[(std::size_t) j] - 0.5f) * p.stereo;
                    auto g = c == 0 ? 0.5f - d : d + 0.5f;
                    g = g + g;
                    g = g < 0.0f ? 0.0f : (g > 1.0f ? 1.0f : g);
                    if (p.vel)
                        g = g * weights[j];
                    hGain[c][j] = g;
                    hShift[c][j] = (float) notes[j] - nd;
                }
        }
    }

    struct PowCache
    {
        float arg = 0.0f, value = 1.0f;
    };
    static float pow2Cached (PowCache& cache, float x) noexcept
    {
        if (x != cache.arg)
        {
            cache.arg = x;
            cache.value = (float) std::pow (2.0, (double) x);
        }
        return cache.value;
    }

    // 4-point cubic Hermite between the 2nd and 3rd points from ⌊pos⌋
    static float interp (const float* buf, int L, float pos) noexcept
    {
        if (pos < 0.0f)
            return 0.0f;
        auto i0 = (int) pos;
        const auto f = pos - (float) i0;
        i0 = i0 >= L ? i0 - L : i0;
        auto i1 = i0 + 1;
        i1 = i1 >= L ? i1 - L : i1;
        auto i2 = i1 + 1;
        i2 = i2 >= L ? i2 - L : i2;
        auto i3 = i2 + 1;
        i3 = i3 >= L ? i3 - L : i3;
        const auto y0 = buf[i0], y1 = buf[i1], y2 = buf[i2], y3 = buf[i3];
        const auto c1 = (y2 - y0) * 0.5f;
        const auto c3 = (y3 - y0) * 0.5f + (y1 - y2) * 1.5f;
        const auto c2 = ((y0 - y1) + c1) - c3;
        return ((c3 * f + c2) * f + c1) * f + y1;
    }

    // grain overlap-add into the output ring from the current position; step 0 = no resampling
    void overlapAdd (int c, int n, float step, float gain) noexcept
    {
        auto* o = outRing[c].data();
        auto pos = W2;
        if (step == 0.0f)
        {
            for (int i = 0; i < n; ++i)
            {
                const auto q = pos >= B ? pos - B : pos;
                o[q] = gain * grain[(std::size_t) i] + o[q];
                pos = q + 1;
            }
            return;
        }
        auto fp = 0.0f;
        while (fp < (float) n)
        {
            const auto i = (int) fp;
            const auto fr = fp - (float) i;
            auto v = (1.0f - fr) * grain[(std::size_t) i];
            if (i < n - 1)
                v = fr * grain[(std::size_t) i + 1] + v;
            fp = fp + step;
            const auto q = pos >= B ? pos - B : pos;
            o[q] = v * gain + o[q];
            pos = q + 1;
        }
    }

    float renderFormant (int c, const float (&shifts)[2][4], const float (&gains)[2][4], float x) noexcept
    {
        const auto* inr = inRing[c].data();
        const auto* lpr = lpRing[c].data();
        const auto P = period[c];
        for (int v = 0; v < 4; ++v)
        {
            const auto sh = shifts[c][v];
            auto f25 = 1.0f;
            if (P != 0.0f)
                f25 = pow2Cached (spacingCache[c][v], sh * -0.083333336f);
            if (--timer[c][v] < 1)
            {
                if (P == 0.0f || ! (std::abs (sh) > 0.0f))
                {
                    // pass-through grain: 256-sample periodic Hann from 257…2 samples back, every 128 samples
                    auto st = W2 - 0x101;
                    if (st < 0)
                        st += B;
                    for (int i = 0; i < 256; ++i)
                    {
                        grain[(std::size_t) i] = hann256[(std::size_t) i] * inr[st];
                        if (++st >= B)
                            st = 0;
                    }
                    overlapAdd (c, 256, 0.0f, G[c][v]);
                    timer[c][v] = 128;
                }
                else
                {
                    // PSOLA grain: 2 periods centred 70 samples before the pitch mark (largest positive local
                    // maximum of the filtered signal within 1.1 periods back from half a grain ago)
                    const auto len = (int) (P + P);
                    const auto h = len >> 1;
                    auto k = h + 1, found = -1, j = W2 - k;
                    auto best = 0.0f;
                    for (auto count = (int) ((double) P * 1.1); count > 0; --count, ++k, --j)
                    {
                        const auto jj = j < 0 ? j + B : j;
                        const auto s = lpr[jj];
                        const auto before = lpr[jj == 0 ? B + 1 : jj - 1]; // (B + 1 is never written: 0)
                        if (best < s && before < s && lpr[jj + 1] < s)
                        {
                            best = s;
                            found = k;
                        }
                    }
                    if (found >= 0)
                    {
                        const auto centre = (W2 - h) - found;
                        auto st = centre - 70;
                        if (st < 0)
                            st = B - 70 + centre;
                        const auto inv = 1.0f / (float) len;
                        for (int i = 0; i < len; ++i)
                        {
                            const auto w = (float) ((1.0 - std::cos ((double) ((float) i * inv) * 6.283185307179586)) * 0.5);
                            grain[(std::size_t) i] = w * inr[st];
                            if (++st >= B)
                                st -= B;
                        }
                        overlapAdd (c, len, genderStep, G[c][v]);
                    }
                    timer[c][v] = (int) (f25 * P);
                }
            }
            G[c][v] = (gains[c][v] - G[c][v]) * 0.007f + G[c][v];
        }
        inRing[c][(std::size_t) W2] = x;
        auto& slot = outRing[c][(std::size_t) W2];
        const auto out = slot;
        slot = 0.0f;
        return out;
    }

    double fs = 44100.0, fmin = 110.0;
    bool initialised = false, formantOn = false, midi = false, bypassAll = false, anyAllowed = true;
    int rangeIndex = 2, N = 0, L = 0, B = 0, capacityN = 0, capacityB = 0;
    float speed = 1.0f, fine = 0.0f, A = 55.0f, genderStep = 1.0f, lastGenderParam = -1.0f;
    int ref = 0;
    std::array<bool, 12> keyBypass {};
    std::array<bool, 128> mask {};
    std::array<float, 128> held {};

    // detector
    std::array<float, 2> dc {};
    std::array<std::array<double, 16>, 2> lp {};
    std::vector<float> hist[2], amdf[2];
    int histPos = 0, frameCount = 0;
    float freq[2] {}, period[2] {}, cls[2] { -1.0f, -1.0f }, target[2] {}, sm[2] {};
    int octave[2] { -1, -1 };

    // harmony (per block)
    float hGain[2][4] {}, hShift[2][4] {};
    bool hMix = false;

    // plain shifter
    std::vector<float> delay[2];
    int W = 0;
    float R[2][4] {}, O[2][4] {}, G[2][4] {};
    int K[2][4] {};
    PowCache rhoCache[2][4] {}, spacingCache[2][4] {};

    // formant shifter
    std::vector<float> inRing[2], lpRing[2], outRing[2], grain;
    int W2 = 0;
    int timer[2][4] {};
    std::array<float, 256> hann256 {};
};
} // namespace lynx::pitcher

#if defined(__clang__)
#pragma float_control(pop)
#elif defined(__GNUC__)
#pragma GCC pop_options
#endif
