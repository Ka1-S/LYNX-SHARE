#include "core/TimeStretch.h"

#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <numeric>

#if defined(__clang__)
 #pragma clang diagnostic push
 #pragma clang diagnostic ignored "-Wpedantic"
 #pragma clang diagnostic ignored "-Wextra-semi"
 #pragma clang diagnostic ignored "-Wshadow"
 #pragma clang diagnostic ignored "-Wunused-parameter"
 #pragma clang diagnostic ignored "-Wsign-compare"
 #pragma clang diagnostic ignored "-Wimplicit-float-conversion"
 #pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(__GNUC__)
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wpedantic"
 #pragma GCC diagnostic ignored "-Wshadow"
 #pragma GCC diagnostic ignored "-Wunused-parameter"
 #pragma GCC diagnostic ignored "-Wsign-compare"
#elif defined(_MSC_VER)
 #pragma warning(push, 0)
#endif
#include "../../third_party/signalsmith/signalsmith-stretch.h"
#if defined(__clang__)
 #pragma clang diagnostic pop
#elif defined(__GNUC__)
 #pragma GCC diagnostic pop
#elif defined(_MSC_VER)
 #pragma warning(pop)
#endif

namespace lynx
{
namespace
{
constexpr double pi = 3.141592653589793;

struct Planar
{
    std::vector<std::vector<float>> ch;
    long frames() const { return ch.empty() ? 0 : (long) ch[0].size(); }
};

Planar planarOf (const SampleData& in)
{
    Planar p;
    const auto n = (std::size_t) in.frames();
    p.ch.emplace_back (in.leftData(), in.leftData() + n);
    if (in.isStereo())
        p.ch.emplace_back (in.rightData(), in.rightData() + n);
    return p;
}

std::shared_ptr<SampleData> sampleOf (Planar&& p, const SampleData& like)
{
    auto out = std::make_shared<SampleData>();
    out->sampleRate = like.sampleRate;
    out->left = std::move (p.ch[0]);
    if (p.ch.size() > 1)
        out->right = std::move (p.ch[1]);
    return out;
}

std::vector<float> monoOf (const Planar& p)
{
    std::vector<float> m (p.ch[0]);
    if (p.ch.size() > 1)
        for (std::size_t i = 0; i < m.size(); ++i)
            m[i] = 0.5f * (m[i] + p.ch[1][i]);
    return m;
}

double besselI0 (double x)
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 40; ++k)
    {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
        if (term < 1e-12 * sum)
            break;
    }
    return sum;
}

// Kaiser-windowed sinc resampling of the whole signal to `outFrames` (used for ratios within 0.1 % of 1, where the
// cutoff stays at Nyquist): 64 taps, β = 9 (stop band beyond −90 dB). Polyphase: the kernel is tabulated for 512
// fractional positions (each row = the 64 taps for that position) and two neighbouring rows are blended, so each output
// frame is two contiguous dot products (vectorised) — a 3-minute stereo take resamples in about a second.
Planar resampleTo (const Planar& in, long outFrames)
{
    constexpr int half = 32, taps = 2 * half;
    constexpr int phases = 512;
    constexpr double beta = 9.0;
    const auto n = in.frames();
    const auto step = (double) n / (double) outFrames; // input frames per output frame
    const auto norm = 1.0 / besselI0 (beta);

    const auto kernel = [&] (double x) {
        const auto r = std::abs (x) / half;
        if (r >= 1.0)
            return 0.0;
        const auto w = besselI0 (beta * std::sqrt (1.0 - r * r)) * norm;
        return (x == 0.0 ? 1.0 : std::sin (pi * x) / (pi * x)) * w;
    };
    // row p, tap k: the weight of input frame i0 − half + 1 + k for the position i0 + p / phases
    std::vector<float> table ((std::size_t) ((phases + 1) * taps));
    for (int p = 0; p <= phases; ++p)
        for (int k = 0; k < taps; ++k)
            table[(std::size_t) (p * taps + k)] = (float) kernel ((double) (k - half + 1) - (double) p / phases);

    Planar out;
    for (const auto& c : in.ch)
    {
        std::vector<float> y ((std::size_t) outFrames);
        for (long j = 0; j < outFrames; ++j)
        {
            const auto pos = (double) j * step;
            const auto i0 = (long) std::floor (pos);
            const auto phase = (pos - (double) i0) * phases;
            const auto p = std::min (phases - 1, (int) phase);
            const auto g = (float) (phase - (double) p);
            const auto* k0 = table.data() + (std::size_t) (p * taps);
            const auto* k1 = k0 + taps;
            const auto first = i0 - half + 1;
            float a0 = 0.0f, a1 = 0.0f;
            if (first >= 0 && first + taps <= n)
            {
                const auto* x = c.data() + first;
                for (int k = 0; k < taps; ++k)
                {
                    a0 += x[k] * k0[k];
                    a1 += x[k] * k1[k];
                }
            }
            else
                for (int k = 0; k < taps; ++k)
                {
                    const auto i = first + k;
                    if (i >= 0 && i < n)
                    {
                        a0 += c[(std::size_t) i] * k0[k];
                        a1 += c[(std::size_t) i] * k1[k];
                    }
                }
            y[(std::size_t) j] = a0 + (a1 - a0) * g;
        }
        out.ch.push_back (std::move (y));
    }
    return out;
}

// Signalsmith Stretch over the whole signal (exact output length, latency compensated by its exact() call).
Planar spectralStretch (const Planar& in, long outFrames, double sampleRate, double blockSeconds, double intervalSeconds)
{
    const auto channels = (int) in.ch.size();
    // a fixed seed (round 165i): beyond 2× the stretcher randomises each bin's time step, and with the default seed from
    // std::random_device the same sample stretched twice — two exports, two opens — came out different
    signalsmith::stretch::SignalsmithStretch<float> stretch { 1651L };
    stretch.configure (channels, (int) std::lround (sampleRate * blockSeconds), (int) std::lround (sampleRate * intervalSeconds));

    // exact() needs at least one block (plus an interval) of input: short sounds are padded with silence and the
    // result cut to length
    const auto ratio = (double) outFrames / (double) in.frames();
    const auto minInput = (long) (stretch.blockSamples() + 2 * stretch.intervalSamples()) * 2;
    Planar padded;
    const Planar* src = &in;
    auto paddedOut = outFrames;
    if (in.frames() < minInput)
    {
        padded = in;
        for (auto& c : padded.ch)
            c.resize ((std::size_t) minInput, 0.0f);
        paddedOut = (long) std::llround ((double) minInput * ratio);
        src = &padded;
    }

    Planar out;
    out.ch.assign ((std::size_t) channels, std::vector<float> ((std::size_t) paddedOut, 0.0f));
    std::vector<const float*> inputs;
    std::vector<float*> outputs;
    for (int c = 0; c < channels; ++c)
    {
        inputs.push_back (src->ch[(std::size_t) c].data());
        outputs.push_back (out.ch[(std::size_t) c].data());
    }
    stretch.exact (inputs.data(), (int) src->frames(), outputs.data(), (int) paddedOut);

    for (auto& c : out.ch)
        c.resize ((std::size_t) outFrames);
    return out;
}

// 64-sample RMS envelope, one value per 16 frames
struct Envelope
{
    static constexpr int hop = 16, width = 64;
    std::vector<float> rms;

    explicit Envelope (const std::vector<float>& x)
    {
        const auto n = (long) x.size();
        rms.resize ((std::size_t) (n / hop + 1));
        for (std::size_t k = 0; k < rms.size(); ++k)
        {
            const auto from = (long) k * hop - width / 2;
            double e = 0.0;
            for (long i = std::max (0L, from); i < std::min (n, from + width); ++i)
                e += (double) x[(std::size_t) i] * x[(std::size_t) i];
            rms[k] = (float) std::sqrt (e / width);
        }
    }
    float at (long frame) const
    {
        const auto k = std::clamp (frame / hop, 0L, (long) rms.size() - 1);
        return rms[(std::size_t) k];
    }
};

double correlation (const float* a, const float* b, long n)
{
    double ab = 0.0, aa = 0.0, bb = 0.0;
    for (long i = 0; i < n; ++i)
    {
        ab += (double) a[i] * b[i];
        aa += (double) a[i] * a[i];
        bb += (double) b[i] * b[i];
    }
    return aa > 0.0 && bb > 0.0 ? ab / std::sqrt (aa * bb) : 0.0;
}

// Puts each detected attack of the input back, unstretched, over the spectral stretcher's output: the original from a
// few ms before the attack to `hold` after it replaces the stretched signal (which smears attacks over its analysis
// block), with short crossfades placed where both signals correlate best.
// `toOut`: where an input frame lands in the output (one ratio, or a warp map)
void spliceAttacks (Planar& out, const Planar& in, const std::function<double (double)>& toOut, double sampleRate, double minRiseDb,
                    double holdSeconds)
{
    const auto mono = monoOf (in);
    const auto attacks = stretchdetail::detectAttacks (mono.data(), (long) mono.size(), sampleRate, minRiseDb);
    if (attacks.empty())
        return;

    const auto n = in.frames();
    const auto m = out.frames();
    const auto fadeIn = std::max (16L, (long) (sampleRate * 0.002));
    const auto fadeOut = std::max (32L, (long) (sampleRate * 0.008));
    const auto pre = (long) (sampleRate * 0.004);
    // the original also replaces up to `preRoll` before the attack (when the previous splice leaves room): the spectral
    // stretcher smears an attack backwards over up to half its block, which a 4 ms pre-roll left audible as pre-echo
    const auto preRoll = (long) (sampleRate * 0.015);
    long lastEnd = 0;
    const auto search = (long) (sampleRate * 0.006);
    const auto holdMax = (long) (sampleRate * holdSeconds);
    const auto outMono = monoOf (out);

    for (std::size_t a = 0; a < attacks.size(); ++a)
    {
        const auto o = attacks[a];
        const auto p = (long) std::llround (toOut ((double) o));
        const auto shift = p - o; // output index = input index + shift inside the spliced region

        // room before the next attack (in output frames) and before the previous splice
        const auto nextOut = a + 1 < attacks.size() ? (long) std::llround (toOut ((double) attacks[a + 1])) : m;
        const auto room = nextOut - p - pre - fadeIn - fadeOut;
        if (room < fadeOut)
            continue;
        const auto hold = std::min (holdMax, room);

        // the end of the unstretched part: where stretched and original agree best (least comb filtering in the fade)
        auto bestEnd = p + hold;
        double bestScore = -2.0;
        for (long e = p + std::max (fadeOut, hold - search); e <= p + hold; e += 8)
        {
            const auto src = e - shift;
            if (e + fadeOut > m || src + fadeOut > n || src < 0)
                break;
            const auto s = correlation (outMono.data() + e, mono.data() + src, fadeOut);
            if (s > bestScore)
            {
                bestScore = s;
                bestEnd = e;
            }
        }

        // fade-in begins here: as early as the pre-roll allows, never before the previous splice's end
        const auto latest = p - pre - fadeIn;
        const auto start = std::max ({ p - preRoll - fadeIn, std::min (lastEnd, latest), shift, 0L });
        if (start > latest)
            continue;
        const auto end = std::min (bestEnd + fadeOut, std::min (m, n + shift));
        lastEnd = end;

        for (std::size_t c = 0; c < out.ch.size(); ++c)
        {
            auto& y = out.ch[c];
            const auto& x = in.ch[c];
            for (long i = start; i < end; ++i)
            {
                double w = 1.0;
                if (i < start + fadeIn)
                    w = 0.5 - 0.5 * std::cos (pi * (double) (i - start) / (double) fadeIn);
                else if (i >= bestEnd)
                    w = 0.5 + 0.5 * std::cos (pi * (double) (i - bestEnd) / (double) fadeOut);
                y[(std::size_t) i] = (float) ((1.0 - w) * y[(std::size_t) i] + w * x[(std::size_t) (i - shift)]);
            }
        }
    }
}

// LYNX's Beats warp mode (round 175a, N4), modelled on Ableton Live's Beats mode by measurement (Live 12.4 renders of a
// test voice at 0.8×, 1.25× speed; /mnt/e/lynx-work/ableton175a/n4): the audio is cut at its attacks and each piece
// keeps its own speed (no stretching at all inside a piece). Slower: when a piece has played to its end before the
// next one starts, it plays on backwards from its end, then forwards again ("back and forth"), until the next piece.
// Faster: the piece is cut where the next one begins. The pieces meet in 16 ms linear crossfades (the length that
// matched Live's spectrum best: 0.2–0.3 dB mean deviation; 1–4 ms left clicks Live does not have).
// Transposed (`semitones` ≠ 0: Live's clip transpose in Beats mode), each piece is resampled — read 2^(st/12) times
// faster from its start, so it is shorter and higher — and still starts at its own place on the grid; the gap up to the
// next piece is filled back and forth as when slower. Rendered offline per clip pitch (round 175a, the coordinator's
// "match Live by resampling each piece").
// With a warp map (`toOut`, round 175a M1) each piece starts where the map puts its attack.
// Round 190gx: the Classic Beats law (WarpModeSettings::beatsLawClassic), kept bit for bit as LYNX rendered it before
// 190gx (rounds 175a / 190fm) so a project saved before then sounds the same: cuts 10 ms before the steepest rise, a
// 16 ms fade-in of every piece, Loop Off reads on 16 ms past the piece, the fills turn at the piece's end, Forward
// restarts the piece, Transient Envelope fades the whole slot ((1 − i/span)^k). The Measured law is beatsStretch below.
Planar beatsStretchClassic (const Planar& in, long outFrames, double sampleRate, double semitones, const std::function<double (double)>& toOut,
                     const WarpModeSettings& modes, double sourceBeats, const std::function<double (double)>& toSource)
{
    const auto n = in.frames();
    const auto mono = monoOf (in);
    const auto division = stretchdetail::preserveDivisionBeats (modes.beatsPreserve);
    auto cuts = division > 0.0 && sourceBeats > 0.0 && toSource ? stretchdetail::gridCuts (n, outFrames, sourceBeats, division, toSource)
                                                                 : stretchdetail::beatsCuts (mono.data(), n, sampleRate, false);
    const auto loopMode = std::min<int> (modes.beatsLoop, 2);
    const auto envelope = std::clamp (modes.beatsEnvelope, 0.0f, 100.0f);
    cuts.insert (cuts.begin(), 0L);
    cuts.push_back (n);
    const auto fade = std::max (16L, (long) std::lround (sampleRate * 0.016));
    const auto speed = semitones == 0.0 ? 1.0 : std::pow (2.0, semitones / 12.0);
    const auto weight = [&] (std::size_t k, long i, long own) {
        float w = 1.0f;
        if (k > 0 && i < fade)
            w = (float) i / (float) fade;
        if (i >= own)
            w *= 1.0f - (float) (i - own) / (float) fade;
        return w;
    };
    // 4-point Hermite inside the piece [a, a + length): sample `at` + `f` of it, its edges held
    const auto cubicAt = [] (const std::vector<float>& x, long a, long length, long at, float f) {
        const auto get = [&] (long j) { return x[(std::size_t) (a + std::clamp (j, 0L, length - 1))]; };
        const auto y0 = get (at - 1), y1 = get (at), y2 = get (at + 1), y3 = get (at + 2);
        const auto c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    };

    Planar out;
    out.ch.assign (in.ch.size(), std::vector<float> ((std::size_t) outFrames, 0.0f));
    for (std::size_t k = 0; k + 1 < cuts.size(); ++k)
    {
        const auto a = cuts[k], length = cuts[k + 1] - a;
        const auto o0 = std::clamp ((long) std::llround (toOut ((double) a)), 0L, outFrames);
        const auto o1 = k + 2 == cuts.size() ? outFrames : std::clamp ((long) std::llround (toOut ((double) cuts[k + 1])), o0, outFrames);
        const auto last = k + 2 == cuts.size();
        const auto span = std::min (outFrames - o0, (o1 - o0) + (last ? 0L : fade));
        if (length <= 0 || span <= 0)
            continue;
        if (speed == 1.0)
        {
            const auto period = 2 * length;
            for (long i = 0; i < span; ++i)
            {
                long src;
                auto w = weight (k, i, o1 - o0);
                if (loopMode == 2)
                {
                    const auto m = i % period;
                    src = a + (m < length ? m : period - 1 - m);
                }
                else if (loopMode == 1)
                    src = a + i % length;
                else
                {
                    if (i >= length + fade || a + i >= n)
                        break;
                    if (i >= length)
                        w *= 1.0f - (float) (i - length) / (float) fade;
                    src = a + i;
                }
                if (envelope < 100.0f)
                    w *= stretchdetail::beatsEnvelopeGainClassic (envelope, i, o1 - o0);
                for (std::size_t c = 0; c < in.ch.size(); ++c)
                    out.ch[c][(std::size_t) (o0 + i)] += w * in.ch[c][(std::size_t) src];
            }
            continue;
        }
        // the piece resampled: `played` output frames read it from a to its end, `speed` source frames per output frame
        const auto played = std::max (1L, (long) std::floor ((double) (length - 1) / speed) + 1);
        const auto period = 2 * played;
        for (long i = 0; i < span; ++i)
        {
            const auto m = i % period;
            auto w = weight (k, i, o1 - o0);
            double t;
            if (loopMode == 2)
                t = (double) (m < played ? m : period - 1 - m) * speed;
            else if (loopMode == 1)
                t = (double) (i % played) * speed;
            else
            {
                if (i >= played)
                    break; // (Loop Off: the piece read once; its last frames are held at the piece's edge, so a short fade)
                t = (double) i * speed;
                if (i >= played - std::min (played, fade))
                    w *= (float) (played - i) / (float) std::min (played, fade);
            }
            if (envelope < 100.0f)
                w *= stretchdetail::beatsEnvelopeGainClassic (envelope, i, o1 - o0);
            const auto at = (long) t;
            const auto f = (float) (t - (double) at);
            for (std::size_t c = 0; c < in.ch.size(); ++c)
                out.ch[c][(std::size_t) (o0 + i)] += w * cubicAt (in.ch[c], a, length, at, f);
        }
    }
    return out;
}

// Round 190fm adds Live's Beats settings (`modes`; the defaults run exactly the code above): Preserve on a note grid
// (cuts where the output reaches each multiple of the division, `sourceBeats` = the beats the source spans, `toSource`
// = the inverse of `toOut`), Transient Loop Mode and Transient Envelope — their laws re-measured in round 190gx (see
// the fill below).
Planar beatsStretch (const Planar& in, long outFrames, double sampleRate, double semitones, const std::function<double (double)>& toOut,
                     const WarpModeSettings& modes = {}, double sourceBeats = 0.0, const std::function<double (double)>& toSource = {})
{
    if (modes.beatsLaw == WarpModeSettings::beatsLawClassic)
        return beatsStretchClassic (in, outFrames, sampleRate, semitones, toOut, modes, sourceBeats, toSource);
    const auto n = in.frames();
    const auto mono = monoOf (in);
    const auto division = stretchdetail::preserveDivisionBeats (modes.beatsPreserve);
    auto cuts = division > 0.0 && sourceBeats > 0.0 && toSource ? stretchdetail::gridCuts (n, outFrames, sourceBeats, division, toSource)
                                                                 : stretchdetail::beatsCuts (mono.data(), n, sampleRate);
    const auto loopMode = std::min<int> (modes.beatsLoop, 2);
    const auto envelope = std::clamp (modes.beatsEnvelope, 0.0f, 100.0f);
    cuts.insert (cuts.begin(), 0L);
    cuts.push_back (n);
    const auto fade = std::max (16L, (long) std::lround (sampleRate * 0.016));
    const auto speed = semitones == 0.0 ? 1.0 : std::pow (2.0, semitones / 12.0);
    // round 190gx: a piece cut at a transient starts almost at once (Live: full level within 1 ms of the cut on the drum
    // loop; 2 ms matched the renders best of 1 / 2 / 4 / 16); a grid piece, cut anywhere in the sound, keeps the 16 ms
    // crossfade (measured: shorter ones click on the voice). The previous piece still fades out over 16 ms.
    const auto onGrid = division > 0.0 && sourceBeats > 0.0 && toSource;
    const auto fadeInLen = onGrid ? fade : std::max (1L, (long) std::lround (sampleRate * 0.002));
    const auto weight = [&] (std::size_t k, long i, long own) {
        float w = 1.0f;
        if (k > 0 && i < fadeInLen)
            w = (float) i / (float) fadeInLen;
        if (i >= own)
            w *= 1.0f - (float) (i - own) / (float) fade;
        return w;
    };
    // 4-point Hermite inside the piece [a, a + length): sample `at` + `f` of it, its edges held
    const auto cubicAt = [] (const std::vector<float>& x, long a, long length, long at, float f) {
        const auto get = [&] (long j) { return x[(std::size_t) (a + std::clamp (j, 0L, length - 1))]; };
        const auto y0 = get (at - 1), y1 = get (at), y2 = get (at + 1), y3 = get (at + 2);
        const auto c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    };
    // Round 190gx (Live 12.4's W1 renders, /mnt/e/lynx-work/livefacts190gx/warp/REPORT.md): what fills the gap after a
    // piece's own end. `own` = the output frames the piece lasts at its own speed. Loop Off: the piece fades out over its
    // last 10 ms and ends there (Live: a linear 10 ms ramp ending exactly at the next transient; nothing after).
    // Back-and-forth and Forward turn `beatsTurnSeconds` before the piece's end (Live: 21–30 ms, the reversed reading
    // mirrors there), Back-and-forth bounces inside the piece's second half, Forward loops that second half (a short
    // crossfade at each jump). Transient Envelope: unity up to the piece's own end, then an exponential decay
    // (stretchdetail::beatsEnvelopeGain).
    struct Read
    {
        double pos = 0.0, pos2 = -1.0; // where in the piece (own-speed frames); pos2 ≥ 0: crossfaded with it by mix
        float mix = 0.0f, gain = 1.0f;
        bool stop = false;
    };
    // (the turn is a transient cut's: on a Preserve grid Live fills from the piece's very end — measured: every grid case
    // matches best with no turn)
    const auto turnFrames = onGrid ? 0L : (long) std::lround (sampleRate * stretchdetail::beatsTurnSeconds);
    const auto loopXfade = std::max (8L, (long) std::lround (sampleRate * 0.005));
    const auto offFade = std::max (8L, (long) std::lround (sampleRate * 0.010));
    const auto readOf = [&] (long i, long own, bool fills) {
        Read r;
        if (! fills) // (the piece's slot is no longer than the piece: it plays straight on, cut by the next one)
            r.pos = (double) i;
        else if (i < own && (loopMode == 0 || i < own - turnFrames || own < 4 * turnFrames))
        {
            r.pos = (double) i;
            if (loopMode == 0 && i >= own - std::min (own, offFade))
                r.gain = (float) (own - i) / (float) std::min (own, offFade);
        }
        else if (loopMode == 0)
            r.stop = true;
        else
        {
            const auto turn = own < 4 * turnFrames ? own : own - turnFrames; // (short pieces: at their end)
            const auto mid = turn / 2, seg = std::max (1L, turn - mid);
            const auto u = i - turn;
            if (loopMode == 2)
            {
                const auto m = u % (2 * seg);
                r.pos = (double) (m < seg ? turn - 1 - m : mid + (m - seg));
            }
            else
            {
                const auto m = u % seg;
                r.pos = (double) (mid + m);
                if (m < loopXfade) // (from the run that just ended, carried on past the turn)
                {
                    r.pos2 = (double) (turn + m);
                    r.mix = (float) m / (float) loopXfade;
                }
            }
        }
        if (envelope < 100.0f && i >= own)
            r.gain *= stretchdetail::beatsEnvelopeGain (envelope, i - own, sampleRate);
        return r;
    };

    Planar out;
    out.ch.assign (in.ch.size(), std::vector<float> ((std::size_t) outFrames, 0.0f));
    bool previousCut = false; // (Loop Off: a piece that follows one ended by its own fade starts at once)
    for (std::size_t k = 0; k + 1 < cuts.size(); ++k)
    {
        const auto a = cuts[k], length = cuts[k + 1] - a;
        const auto o0 = std::clamp ((long) std::llround (toOut ((double) a)), 0L, outFrames);
        const auto o1 = k + 2 == cuts.size() ? outFrames : std::clamp ((long) std::llround (toOut ((double) cuts[k + 1])), o0, outFrames);
        const auto last = k + 2 == cuts.size();
        const auto span = std::min (outFrames - o0, (o1 - o0) + (last ? 0L : fade));
        if (length <= 0 || span <= 0)
            continue;
        // the piece at its own speed lasts `own` output frames (resampled: read from a to its end, `speed` source frames
        // per output frame)
        const auto own = speed == 1.0 ? length : std::max (1L, (long) std::floor ((double) (length - 1) / speed) + 1);
        const auto fills = o1 - o0 > own;
        const auto fadeIn = loopMode != 0 || previousCut;
        previousCut = ! fills;
        for (long i = 0; i < span; ++i)
        {
            const auto r = readOf (i, own, fills);
            if (r.stop)
                break;
            auto w = (fadeIn ? weight (k, i, o1 - o0) : (i >= o1 - o0 ? 1.0f - (float) (i - o1 + o0) / (float) fade : 1.0f)) * r.gain;
            for (std::size_t c = 0; c < in.ch.size(); ++c)
            {
                const auto sample = [&] (double pos) {
                    if (speed == 1.0)
                        return in.ch[c][(std::size_t) std::min (n - 1, a + (long) pos)];
                    const auto t = pos * speed;
                    const auto at = (long) t;
                    return cubicAt (in.ch[c], a, length, at, (float) (t - (double) at));
                };
                auto v = sample (r.pos);
                if (r.pos2 >= 0.0)
                    v = r.mix * v + (1.0f - r.mix) * sample (r.pos2);
                out.ch[c][(std::size_t) (o0 + i)] += w * v;
            }
        }
    }
    return out;
}
} // namespace

namespace stretchdetail
{
std::vector<long> beatsCuts (const float* mono, long n, double sampleRate, bool measured)
{
    auto cuts = detectAttacks (mono, n, sampleRate, 6.0);
    // each cut moves to where the attack rises fastest: the start of the 10 ms window before the steepest 10 ms rise of
    // the RMS level, within −30…+50 ms (Live cuts a tone at its very start and a swell at its steepest point; LYNX's
    // attack detector marks the half-peak point, 16 ms late on a tone and 37 ms early on a swell) [measured]
    {
        std::vector<double> sum ((std::size_t) n + 1, 0.0);
        for (long i = 0; i < n; ++i)
            sum[(std::size_t) i + 1] = sum[(std::size_t) i] + (double) mono[i] * mono[i];
        const auto w = std::max (8L, (long) std::lround (sampleRate * 0.010));
        const auto hop = std::max (1L, (long) std::lround (sampleRate * 0.0005));
        const auto rms = [&] (long at) { return std::sqrt ((sum[(std::size_t) (at + w)] - sum[(std::size_t) at]) / (double) w); };
        for (auto& c : cuts)
        {
            const auto from = std::max (0L, c - (long) (sampleRate * 0.030));
            const auto to = std::min (n - 2 * w, c + (long) (sampleRate * 0.050));
            double steepest = -1.0;
            for (long k = from; k < to; k += hop)
                if (const auto rise = rms (k + w) - rms (k); rise > steepest)
                {
                    steepest = rise;
                    c = measured ? k + w : k; // (round 190gx: the start of the steepest window, i.e. the attack itself —
                                              // Live cuts the drum loop's hits on their onsets; the Classic law's window
                                              // before it put LYNX's cuts 10 ms early)
                }
        }
        std::sort (cuts.begin(), cuts.end());
        const auto minGap = (long) (sampleRate * 0.010);
        cuts.erase (std::unique (cuts.begin(), cuts.end(), [minGap] (long a, long b) { return b - a < minGap; }), cuts.end());
    }
    cuts.erase (std::remove_if (cuts.begin(), cuts.end(), [n] (long c) { return c <= 0 || c >= n; }), cuts.end());
    return cuts;
}

std::vector<long> detectAttacks (const float* mono, long frames, double sampleRate, double minRiseDb)
{
    // An attack = the (high-passed) RMS envelope rising by at least `minRiseDb` within ~3 ms over its level just before, above an
    // absolute floor; at most one per 50 ms (the strongest).
    std::vector<long> found;
    if (frames < 256)
        return found;

    // the envelope of the first difference: attacks carry high frequencies, the low-frequency tail of the previous hit
    // (a kick, a bass) does not mask them
    std::vector<float> slope ((std::size_t) frames);
    slope[0] = mono[0];
    for (long i = 1; i < frames; ++i)
        slope[(std::size_t) i] = mono[i] - mono[i - 1];
    const Envelope env (slope);
    const auto& r = env.rms;
    const auto peak = *std::max_element (r.begin(), r.end());
    if (peak <= 0.0f)
        return found;

    const auto floorLevel = peak * 0.02f; // −34 dB below the loudest moment
    const auto riseFactor = (float) std::pow (10.0, minRiseDb / 20.0);
    const auto ahead = std::max (1L, (long) (sampleRate * 0.003) / Envelope::hop);
    const auto behind = std::max (1L, (long) (sampleRate * 0.010) / Envelope::hop);
    const auto minGap = (long) (sampleRate * 0.05);

    struct Candidate
    {
        long frame;
        float rise;
    };
    std::vector<Candidate> candidates;
    for (long k = behind; k + ahead < (long) r.size(); ++k)
    {
        // level before: the maximum over the previous 10 ms (a decaying tail is not an attack)
        float before = 0.0f;
        for (long j = k - behind; j < k; ++j)
            before = std::max (before, r[(std::size_t) j]);
        const auto after = r[(std::size_t) (k + ahead)];
        if (after < floorLevel)
            continue;
        const auto rise = after / std::max (before, peak * 1e-4f);
        if (rise >= riseFactor)
            candidates.push_back ({ k * Envelope::hop, rise });
    }

    // keep the strongest of each cluster, at least `minGap` apart
    std::sort (candidates.begin(), candidates.end(), [] (const Candidate& a, const Candidate& b) { return a.rise > b.rise; });
    for (const auto& c : candidates)
        if (std::none_of (found.begin(), found.end(), [&] (long f) { return std::abs (f - c.frame) < minGap; }))
            found.push_back (c.frame);
    std::sort (found.begin(), found.end());

    // to the sample: the first 8-frame step whose level reaches half the attack's peak (the time map is exact only at
    // the real start of the attack; the envelope frame can be a few ms early)
    const auto look = (long) (sampleRate * 0.012);
    for (auto& f : found)
    {
        const auto from = std::max (0L, f - (long) (sampleRate * 0.002));
        const auto to = std::min (frames - 8, f + look);
        double peakStep = 0.0;
        for (long i = from; i < to; i += 8)
        {
            double e = 0.0;
            for (long j = i; j < i + 8; ++j)
                e = std::max (e, (double) std::abs (mono[j]));
            peakStep = std::max (peakStep, e);
        }
        for (long i = from; i < to; i += 8)
        {
            double e = 0.0;
            long at = i;
            for (long j = i; j < i + 8; ++j)
                if (std::abs (mono[j]) > e)
                {
                    e = std::abs (mono[j]);
                    at = j;
                }
            if (e >= 0.5 * peakStep)
            {
                f = at;
                break;
            }
        }
    }
    return found;
}

Preset presetFor (StretchMethod method) noexcept
{
    // Measured in round 160 (REA_rodada160 §2) against synthetic signals with a known ideal result: an overlap of 6
    // (interval = block / 6) instead of Signalsmith's default 4 lowers the spectral error on chords and voices by about
    // a third and the tonal noise by 20…40 dB; short blocks keep drums and voices tighter, long ones chords cleaner.
    switch (method)
    {
        case StretchMethod::transients: return { 0.06, 0.01, 6.0, 0.030 };
        case StretchMethod::mono: return { 0.06, 0.01, 9.0, 0.020 };
        // LYNX's warp modes (round 175a, N4), fitted to Live 12's Tones / Texture / Complex renders of a voice at
        // 0.8× and 1.25× (envelope correlation / mean spectral deviation): no attack splicing (it doubled Live's
        // spectral deviation), tones 40 ms blocks (0.977 / 1.06 dB), texture 60 ms (0.985 / 0.98 dB), complex 90 ms
        // (0.994 / 0.88 dB; full mixes and chords want long blocks, REA_rodada160 §2). Beats has its own engine
        // (beatsStretch); its preset only picks the voice pitch shifter's block.
        case StretchMethod::beats: return { 0.06, 0.01, 6.0, 0.030 };
        case StretchMethod::tones: return { 0.04, 0.04 / 6.0, 0.0, 0.0 };
        case StretchMethod::texture: return { 0.06, 0.01, 0.0, 0.0 };
        case StretchMethod::complex: return { 0.09, 0.015, 0.0, 0.0 };
        case StretchMethod::general: break;
    }
    return { 0.12, 0.02, 9.0, 0.020 };
}

Preset presetFor (StretchMethod method, const WarpModeSettings& modes) noexcept
{
    auto preset = presetFor (method);
    const auto scaled = [&] (float size, float defaultSize) {
        if (size == defaultSize || ! (size > 0.0f))
            return;
        const auto block = std::clamp (preset.blockSeconds * (double) size / (double) defaultSize, 0.010, 0.250);
        preset.intervalSeconds *= block / preset.blockSeconds; // (the same overlap)
        preset.blockSeconds = block;
    };
    if (method == StretchMethod::tones)
        scaled (modes.tonesGrain, WarpModeSettings::defaultTonesGrain);
    else if (method == StretchMethod::texture)
        scaled (modes.textureGrain, WarpModeSettings::defaultTextureGrain);
    return preset;
}

double preserveDivisionBeats (int preserve) noexcept
{
    static constexpr double beats[] = { 4.0, 2.0, 1.0, 0.5, 0.25, 0.125 };
    return preserve >= 0 && preserve < 6 ? beats[preserve] : 0.0;
}

std::vector<long> gridCuts (long frames, long outFrames, double sourceBeats, double divisionBeats, const std::function<double (double)>& toSource)
{
    std::vector<long> cuts;
    if (frames < 2 || outFrames < 2 || ! (sourceBeats > 0.0) || ! (divisionBeats > 0.0))
        return cuts;
    const auto perBeat = (double) outFrames / sourceBeats;
    const auto minGap = std::max (1L, frames / 100000);
    for (long k = 1;; ++k)
    {
        const auto y = (double) k * divisionBeats * perBeat;
        if (y >= (double) outFrames - 0.5 || k > 1000000)
            break;
        const auto x = (long) std::llround (toSource (y));
        if (x <= 0 || x >= frames || (! cuts.empty() && x - cuts.back() < minGap))
            continue;
        cuts.push_back (x);
    }
    return cuts;
}

float beatsEnvelopeGainClassic (float envelope, long i, long span) noexcept
{
    const auto e = std::clamp ((double) envelope / 100.0, 0.0, 1.0);
    if (e >= 1.0 || span <= 0)
        return 1.0f;
    const auto k = 4.0 * (1.0 - e) / std::max (e, 0.02);
    const auto x = std::clamp (1.0 - (double) i / (double) span, 0.0, 1.0);
    return (float) std::pow (x, k);
}

float beatsEnvelopeGain (float envelope, long sinceEnd, double sampleRate) noexcept
{
    const auto e = std::clamp ((double) envelope, 0.0, 100.0);
    if (e >= 100.0 || sinceEnd <= 0 || ! (sampleRate > 0.0))
        return 1.0f;
    const auto tau = beatsEnvelopeSeconds (envelope);
    return (float) std::exp (-(double) sinceEnd / (tau * sampleRate));
}

double beatsEnvelopeSeconds (float envelope) noexcept
{
    // Live 12.4 (W1 renders, 190gx): 0 → 15.5 ms (0.560 dB/ms), 50 → 98.0 ms (0.0886 dB/ms), the same on every piece of
    // the drum loop and the voice whatever the gap's length; between them and up to 100 a geometric law (the knob's
    // time mapping is an assumption: only 0 and 50 were rendered), 100 = no decay
    const auto e = std::clamp ((double) envelope, 0.0, 100.0);
    return 0.0155 * std::pow (0.0980 / 0.0155, e / 50.0);
}

std::shared_ptr<SampleData> stretchWith (const SampleData& in, double ratio, const Preset& preset)
{
    const auto planar = planarOf (in);
    const auto frames = planar.frames();
    const auto outFrames = std::max (1L, (long) std::llround ((double) frames * ratio));
    auto out = spectralStretch (planar, outFrames, in.sampleRate, preset.blockSeconds, preset.intervalSeconds);
    if (preset.attackRiseDb > 0.0)
    {
        const auto ratio = (double) outFrames / (double) frames;
        spliceAttacks (out, planar, [ratio] (double x) { return x * ratio; }, in.sampleRate, preset.attackRiseDb, preset.holdSeconds);
    }
    return sampleOf (std::move (out), in);
}
} // namespace stretchdetail

struct VoicePitchShifter::Impl
{
    signalsmith::stretch::SignalsmithStretch<float> stretch { 162L }; // fixed seed: renders repeat exactly
};

VoicePitchShifter::VoicePitchShifter() = default;
VoicePitchShifter::~VoicePitchShifter() = default;

void VoicePitchShifter::allocate (double sampleRate, StretchMethod method)
{
    impl = std::make_unique<Impl>();
    // real time (measured round 162): 100 ms blocks, overlap 4 — pitch exact to 0.05 cent down to 55 Hz, about 2 % of a
    // core per voice and ~1 ms to prime a note (60 ms blocks missed low notes by 40 cents); drums: 80 ms
    const auto block = method == StretchMethod::transients || method == StretchMethod::beats ? 0.08 : 0.1;
    impl->stretch.configure (2, (int) std::lround (sampleRate * block), (int) std::lround (sampleRate * block / 4.0));
    // one silent run sizes every internal buffer, so the audio thread's first note does not allocate
    std::vector<float> silence ((std::size_t) std::max (maxPrimeLength(), 1024), 0.0f), out (256, 0.0f);
    start (silence.data(), silence.data(), primeLength (1.5));
    process (silence.data(), silence.data(), 85, out.data(), out.data() + 128, 128);
}

bool VoicePitchShifter::ready() const noexcept { return impl != nullptr; }

int VoicePitchShifter::primeLength (double factor) const noexcept
{
    if (impl == nullptr)
        return 0;
    factor = std::clamp (factor, minFactor, maxFactor);
    return impl->stretch.outputSeekLength ((float) (1.0 / factor)) + 1;
}

void VoicePitchShifter::start (const float* l, const float* r, int frames) noexcept
{
    if (impl == nullptr)
        return;
    const float* in[2] { l, r };
    impl->stretch.outputSeek (in, frames);
}

void VoicePitchShifter::process (const float* inL, const float* inR, int inFrames, float* outL, float* outR, int outFrames) noexcept
{
    if (impl == nullptr)
        return;
    const float* in[2] { inL, inR };
    float* out[2] { outL, outR };
    impl->stretch.process (in, inFrames, out, outFrames);
}

ShifterStock::ShifterStock (double sampleRate, bool offline) : rate (sampleRate), makesOnDemand (offline && ! shifterstock::strict()) {}

ShifterStock::~ShifterStock()
{
    for (auto& kind : slots)
        for (auto& slot : kind)
            delete slot.exchange (nullptr);
}

std::unique_ptr<VoicePitchShifter> ShifterStock::make (int kind) const
{
    const KeepFloatMode keep;
    mode.applyHere();
    auto shifter = std::make_unique<VoicePitchShifter>();
    shifter->allocate (rate, kind == 1 ? StretchMethod::transients : StretchMethod::general);
    return shifter;
}

int ShifterStock::maxPrimeLength (int kind) const
{
    // (a function of the rate and the block only: measured once per stock and kind)
    static std::mutex lock;
    static std::vector<std::pair<std::pair<double, int>, int>> known;
    const std::scoped_lock l (lock);
    for (const auto& [key, length] : known)
        if (key.first == rate && key.second == kind)
            return length;
    const auto length = make (kind)->maxPrimeLength();
    known.push_back ({ { rate, kind }, length });
    return length;
}

void ShifterStock::setReserve (int kind, int count)
{
    const std::scoped_lock l (fill);
    wanted[(std::size_t) std::clamp (kind, 0, kinds - 1)] = std::clamp (count, 0, capacity);
}

void ShifterStock::topUp()
{
    const std::scoped_lock l (fill);
    for (int kind = 0; kind < kinds; ++kind)
    {
        auto missing = wanted[(std::size_t) kind] - ready (kind);
        for (auto& slot : slots[(std::size_t) kind])
        {
            if (missing <= 0)
                break;
            if (slot.load (std::memory_order_acquire) != nullptr)
                continue;
            auto fresh = make (kind);
            VoicePitchShifter* expected = nullptr;
            if (slot.compare_exchange_strong (expected, fresh.get(), std::memory_order_release))
            {
                fresh.release();
                --missing;
            }
        }
    }
}

std::unique_ptr<VoicePitchShifter> ShifterStock::take (int kind) noexcept
{
    kind = std::clamp (kind, 0, kinds - 1);
    for (auto& slot : slots[(std::size_t) kind])
        if (slot.load (std::memory_order_relaxed) != nullptr)
            if (auto* s = slot.exchange (nullptr, std::memory_order_acq_rel))
            {
                taken.fetch_add (1, std::memory_order_relaxed);
                return std::unique_ptr<VoicePitchShifter> (s);
            }
    if (makesOnDemand) // an export's own rack: not a real-time thread
    {
        try
        {
            taken.fetch_add (1, std::memory_order_relaxed);
            return make (kind);
        }
        catch (...)
        {
        }
    }
    misses.fetch_add (1, std::memory_order_relaxed);
    return nullptr;
}

int ShifterStock::ready (int kind) const noexcept
{
    int n = 0;
    for (const auto& slot : slots[(std::size_t) std::clamp (kind, 0, kinds - 1)])
        n += slot.load (std::memory_order_relaxed) != nullptr;
    return n;
}

namespace shifterstock
{
namespace
{
std::mutex& registryLock()
{
    static std::mutex m;
    return m;
}
std::vector<std::weak_ptr<ShifterStock>>& registry()
{
    static std::vector<std::weak_ptr<ShifterStock>> r;
    return r;
}
} // namespace

void enlist (const std::shared_ptr<ShifterStock>& stock)
{
    if (stock == nullptr)
        return;
    const std::scoped_lock l (registryLock());
    auto& r = registry();
    r.erase (std::remove_if (r.begin(), r.end(), [] (const std::weak_ptr<ShifterStock>& w) { return w.expired(); }), r.end());
    for (const auto& w : r)
        if (w.lock() == stock)
            return;
    r.push_back (stock);
}

void topUpAll()
{
    std::vector<std::shared_ptr<ShifterStock>> live;
    {
        const std::scoped_lock l (registryLock());
        for (const auto& w : registry())
            if (auto s = w.lock())
                live.push_back (std::move (s));
    }
    for (const auto& s : live)
        s->topUp();
}

namespace
{
std::atomic<bool>& plannedFlag() noexcept
{
    static std::atomic<bool> on { [] {
        const char* e = std::getenv ("LYNX_SHIFTER_STOCK");
        return e == nullptr || e[0] != '0';
    }() };
    return on;
}
} // namespace

bool planned() noexcept { return plannedFlag().load (std::memory_order_relaxed); }
void setPlanned (bool on) noexcept { plannedFlag().store (on, std::memory_order_relaxed); }

bool strict() noexcept
{
    static const bool on = [] {
        const char* e = std::getenv ("LYNX_SHIFTER_STRICT");
        return e != nullptr && e[0] == '1';
    }();
    return on;
}
} // namespace shifterstock

StretchMethod stretchMethodForFlMode (int flMode) noexcept
{
    switch (flMode)
    {
        case 2:  // E3 mono
        case 8:  // E2 mono
        case 9:  // E2 speech
            return StretchMethod::mono;
        case 3:  // slice stretch
        case 4:  // slice map
        case 7:  // E2 transient
            return StretchMethod::transients;
        default: // Stretch Pro (−2), Stretch (−1), E3 generic (1), Auto (5), E2 generic (6)
            return StretchMethod::general;
    }
}

std::shared_ptr<SampleData> timeStretchSpan (const SampleData& span, double ratio, StretchMethod method, double semitones,
                                             const StretchOptions& options)
{
    auto out = timeStretch (span, ratio, method, semitones, options);
    if (out == nullptr)
        return out;
    // FL ends a cut span at zero: after a trimmed, stretched span its clip is silent to the clip's end (FL render p62, round
    // 180d), where the stretcher's last frame is not zero and the clip would hold it as DC. A raised-cosine over 1 ms, as
    // FL's region declick (FUN_006a0760).
    const auto fade = std::min<long> ((long) out->frames(), (long) std::llround (out->sampleRate * 0.001));
    for (long i = 0; i < fade; ++i)
    {
        const auto g = (float) (0.5 - 0.5 * std::cos (3.141592653589793 * (double) i / (double) std::max (1L, fade)));
        const auto at = (std::size_t) ((long) out->frames() - 1 - i);
        out->left[at] *= g;
        if (! out->right.empty())
            out->right[at] *= g;
    }
    copySpanInfo (span, *out);
    return out;
}

void copySpanInfo (const SampleData& span, SampleData& out)
{
    out.cropped = span.cropped;
    out.cropFrom = span.cropFrom;
    out.cropTo = span.cropTo;
    out.acidBeats = span.acidBeats;
    if (span.loopStart >= 0 && span.loopEnd > span.loopStart)
    {
        const auto scale = (double) out.frames() / (double) std::max<std::size_t> (1, span.frames());
        out.loopStart = (std::int64_t) std::llround ((double) span.loopStart * scale);
        out.loopEnd = std::max<std::int64_t> (out.loopStart + 1, (std::int64_t) std::llround ((double) span.loopEnd * scale));
    }
}

std::shared_ptr<SampleData> timeStretch (const SampleData& in, double ratio, StretchMethod method, double semitones,
                                         const StretchOptions& options)
{
    const auto frames = (long) in.frames();
    if (method != StretchMethod::beats)
        semitones = 0.0;

    if (frames >= 4 && ratio > 0.0 && semitones != 0.0)
    {
        const auto outFrames = std::max (1L, (long) std::llround ((double) frames * std::clamp (ratio, 0.05, 20.0)));
    {
        const auto r = (double) outFrames / (double) frames;
        return sampleOf (beatsStretch (planarOf (in), outFrames, in.sampleRate, std::clamp (semitones, -48.0, 48.0), [r] (double x) { return x * r; },
                                       options.modes, options.sourceBeats, [r] (double y) { return y / r; }), in);
    }
    }

    if (frames < 4 || ! (ratio > 0.0) || std::abs (ratio - 1.0) < 1e-9)
    {
        auto out = std::make_shared<SampleData>();
        out->sampleRate = in.sampleRate;
        out->left.assign (in.leftData(), in.leftData() + frames);
        if (in.isStereo())
            out->right.assign (in.rightData(), in.rightData() + frames);
        return out;
    }

    ratio = std::clamp (ratio, 0.05, 20.0);
    const auto outFrames = std::max (1L, (long) std::llround ((double) frames * ratio));
    const auto planar = planarOf (in);

    if (std::abs (ratio - 1.0) <= stretchdetail::resampleTolerance)
        return sampleOf (resampleTo (planar, outFrames), in);

    const auto r = (double) outFrames / (double) frames;
    const auto linear = [r] (double x) { return x * r; };
    if (method == StretchMethod::beats)
        return sampleOf (beatsStretch (planar, outFrames, in.sampleRate, 0.0, linear, options.modes, options.sourceBeats, [r] (double y) { return y / r; }), in);

    const auto preset = stretchdetail::presetFor (method, options.modes);
    auto out = spectralStretch (planar, outFrames, in.sampleRate, preset.blockSeconds, preset.intervalSeconds);
    if (preset.attackRiseDb > 0.0)
        spliceAttacks (out, planar, linear, in.sampleRate, preset.attackRiseDb, preset.holdSeconds);
    return sampleOf (std::move (out), in);
}

// ---------------------------------------------------------------------------------------------------------------
// Warp maps (round 175a, warp markers M1)

double stretchMapOutput (const StretchMap& map, double x) noexcept
{
    if (map.size() < 2)
        return x;
    std::size_t i = 1;
    while (i + 1 < map.size() && x > map[i].first)
        ++i;
    const auto& a = map[i - 1];
    const auto& b = map[i];
    return a.second + (x - a.first) * (b.second - a.second) / std::max (1e-12, b.first - a.first);
}

double stretchMapSource (const StretchMap& map, double y) noexcept
{
    if (map.size() < 2)
        return y;
    std::size_t i = 1;
    while (i + 1 < map.size() && y > map[i].second)
        ++i;
    const auto& a = map[i - 1];
    const auto& b = map[i];
    return a.first + (y - a.second) * (b.first - a.first) / std::max (1e-12, b.second - a.second);
}

StretchMap warpStretchMap (const std::vector<WarpPoint>& warp, double frames, double sampleRate, double outFrames)
{
    if (warp.size() < 2 || ! (frames > 0.0) || ! (sampleRate > 0.0) || ! (outFrames > 0.0))
        return {};
    // the markers, sorted, both coordinates strictly increasing
    std::vector<WarpPoint> w (warp);
    std::sort (w.begin(), w.end(), [] (const WarpPoint& a, const WarpPoint& b) { return a.seconds < b.seconds; });
    std::vector<WarpPoint> m;
    for (const auto& p : w)
        if (m.empty() || (p.seconds > m.back().seconds + 1e-9 && p.beats > m.back().beats + 1e-9))
            m.push_back (p);
    if (m.size() < 2)
        return {};
    StretchMap beats; // sample seconds → beats
    for (const auto& p : m)
        beats.push_back ({ p.seconds, p.beats });
    const auto end = frames / sampleRate;
    const auto b0 = stretchMapOutput (beats, 0.0), b1 = stretchMapOutput (beats, end);
    if (! (b1 > b0))
        return {};
    const auto scale = outFrames / (b1 - b0);
    StretchMap map { { 0.0, 0.0 } };
    for (const auto& p : m)
        if (p.seconds > 0.0 && p.seconds < end)
        {
            const auto x = p.seconds * sampleRate, y = (p.beats - b0) * scale;
            if (x > map.back().first + 0.5 && y > map.back().second + 0.5)
                map.push_back ({ x, y });
        }
    if (map.size() > 1 && (! (outFrames > map.back().second + 0.5) || ! (frames > map.back().first + 0.5)))
        map.pop_back();
    map.push_back ({ frames, outFrames });
    // one tempo: no map (the plain stretch, and the same cached file as before)
    const auto overall = outFrames / frames;
    bool bends = false;
    for (std::size_t i = 1; i < map.size(); ++i)
    {
        const auto r = (map[i].second - map[i - 1].second) / (map[i].first - map[i - 1].first);
        bends = bends || std::abs (r / overall - 1.0) > 1e-4;
    }
    return bends && map.size() >= 3 ? map : StretchMap {};
}

namespace
{
// Signalsmith Stretch along a warp map, in one pass (no seams): the stretcher's playback rate follows the map, 256
// output frames at a time. As exact() does for one ratio, the input runs its analysis latency ahead and the output is
// pre-rolled by outputSeek; past the end the input is silence, and the output is cut to the map's length.
Planar spectralStretchMapped (const Planar& in, const StretchMap& map, double sampleRate, double blockSeconds, double intervalSeconds)
{
    const auto channels = (int) in.ch.size();
    const auto outFrames = (long) std::llround (map.back().second);
    signalsmith::stretch::SignalsmithStretch<float> stretch { 1651L };
    stretch.configure (channels, (int) std::lround (sampleRate * blockSeconds), (int) std::lround (sampleRate * intervalSeconds));
    const auto inLatency = (double) stretch.inputLatency(), outLatency = (double) stretch.outputLatency();
    const auto inputFor = [&] (double outputFrame) { return (long) std::llround (stretchMapSource (map, outputFrame) + inLatency); };

    Planar src = in;
    const auto need = std::max (in.frames(), inputFor ((double) outFrames + outLatency) + 16);
    for (auto& c : src.ch)
        c.resize ((std::size_t) need, 0.0f);
    std::vector<const float*> inputs ((std::size_t) channels);
    const auto at = [&] (long frame) {
        for (int c = 0; c < channels; ++c)
            inputs[(std::size_t) c] = src.ch[(std::size_t) c].data() + frame;
        return inputs.data();
    };

    Planar out;
    out.ch.assign ((std::size_t) channels, std::vector<float> ((std::size_t) outFrames, 0.0f));
    std::vector<float*> outputs ((std::size_t) channels);

    auto inPos = std::max (0L, inputFor (outLatency));
    stretch.outputSeek (at (0), (int) inPos);
    constexpr long chunk = 256;
    for (long o = 0; o < outFrames; o += chunk)
    {
        const auto length = std::min (chunk, outFrames - o);
        const auto inEnd = std::clamp (inputFor ((double) (o + length) + outLatency), inPos, need);
        for (int c = 0; c < channels; ++c)
            outputs[(std::size_t) c] = out.ch[(std::size_t) c].data() + o;
        stretch.process (at (inPos), (int) (inEnd - inPos), outputs.data(), (int) length);
        inPos = inEnd;
    }
    return out;
}
} // namespace

std::shared_ptr<SampleData> timeStretchMapped (const SampleData& in, const StretchMap& map, StretchMethod method, double semitones,
                                               const StretchOptions& options)
{
    const auto frames = (long) in.frames();
    const auto plain = [&] {
        const auto ratio = frames > 0 && ! map.empty() ? map.back().second / (double) frames : 1.0;
        return timeStretch (in, ratio, method, semitones, options);
    };
    if (map.size() < 3 || frames < 4 || std::abs (map.back().first - (double) frames) > 0.5 || map.front() != std::pair<double, double> { 0.0, 0.0 })
        return plain();
    for (std::size_t i = 1; i < map.size(); ++i)
    {
        const auto dx = map[i].first - map[i - 1].first, dy = map[i].second - map[i - 1].second;
        if (! (dx > 0.0) || ! (dy > 0.0) || dy / dx < 0.05 || dy / dx > 20.0)
            return plain();
    }
    if (method != StretchMethod::beats)
        semitones = 0.0;
    const auto outFrames = std::max (1L, (long) std::llround (map.back().second));
    const auto planar = planarOf (in);
    const auto toOut = [&map] (double x) { return stretchMapOutput (map, x); };
    if (method == StretchMethod::beats)
        return sampleOf (beatsStretch (planar, outFrames, in.sampleRate, std::clamp (semitones, -48.0, 48.0), toOut, options.modes,
                                       options.sourceBeats, [&map] (double y) { return stretchMapSource (map, y); }), in);

    const auto preset = stretchdetail::presetFor (method, options.modes);
    auto out = spectralStretchMapped (planar, map, in.sampleRate, preset.blockSeconds, preset.intervalSeconds);
    if (preset.attackRiseDb > 0.0)
        spliceAttacks (out, planar, toOut, in.sampleRate, preset.attackRiseDb, preset.holdSeconds);
    return sampleOf (std::move (out), in);
}
StretchOptions stretchOptionsOf (const SamplerSettings& sampler)
{
    return { sampler.warpModes, (double) sampler.stretchBeats };
}

std::string stretchOptionsKey (const StretchOptions& o, StretchMethod method)
{
    const WarpModeSettings d;
    std::string key;
    const auto num = [] (double v) {
        char text[32];
        std::snprintf (text, sizeof text, "%.6g", v);
        return std::string (text);
    };
    if (method == StretchMethod::beats)
    {
        if (o.modes.beatsLaw == WarpModeSettings::beatsLawClassic) // (round 190gx: the law before 190gx)
            key += "|classic";
        if (o.modes.beatsPreserve != d.beatsPreserve && stretchdetail::preserveDivisionBeats (o.modes.beatsPreserve) > 0.0)
            key += "|bp" + std::to_string ((int) o.modes.beatsPreserve) + "@" + num (o.sourceBeats);
        if (std::min<int> (o.modes.beatsLoop, 2) != d.beatsLoop)
            key += "|bl" + std::to_string (std::min<int> (o.modes.beatsLoop, 2));
        if (std::clamp (o.modes.beatsEnvelope, 0.0f, 100.0f) < 100.0f)
            key += "|be" + num (o.modes.beatsEnvelope);
    }
    else if (method == StretchMethod::tones && o.modes.tonesGrain != d.tonesGrain)
        key += "|tg" + num (o.modes.tonesGrain);
    else if (method == StretchMethod::texture && o.modes.textureGrain != d.textureGrain)
        key += "|xg" + num (o.modes.textureGrain);
    return key;
}
} // namespace lynx
