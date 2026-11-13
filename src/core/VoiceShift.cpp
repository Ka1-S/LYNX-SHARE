#include "core/VoiceShift.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <vector>

#include "core/Effects.h"
#include "core/MixSuite.h"
#include "core/VocalPitch.h"

namespace lynx::voiceshift
{
double pitchSemitones (float v) noexcept { return -12.0 + 24.0 * (double) std::clamp (v, 0.0f, 1.0f); }
double formantSemitones (float v) noexcept { return pitchSemitones (v); }
float semitoneValue (double st) noexcept { return (float) std::clamp ((st + 12.0) / 24.0, 0.0, 1.0); }
Mode modeOf (float v) noexcept { return (Mode) std::clamp ((int) std::lround (std::clamp (v, 0.0f, 1.0f) * 2.0f), 0, 2); }
double driveAmount (float v) noexcept { return 10.0 * (double) std::clamp (v, 0.0f, 1.0f); }

int midiChannelIndex (float v) noexcept
{
    const auto k = (int) std::lround (std::clamp (v, 0.0f, 1.0f) * 128.0f);
    return k <= 0 ? -1 : k - 1;
}
float midiChannelValue (int channel) noexcept { return channel < 0 ? 0.0f : (float) std::clamp (channel + 1, 0, 128) / 128.0f; }

int latencyFor (double sampleRate) noexcept { return (int) std::ceil (2.0 * sampleRate / minHz) + 4; }

// [EXEC trial, 1 kHz at −18 dBFS] the drive raises the level by ≈ 1.75 dB per step up to ~5, then saturates
// (H2 ≈ −30 dBc at 5, H3 takes over near 10) and the stage pulls the level back above ~6. A tube curve (Ember's biased
// tanh: even and odd harmonics) after a pre-gain of 1.75 dB per step, with that make-down; 0 = clean.
float driveCurve (float x, double amount) noexcept
{
    if (amount <= 0.0)
        return x;
    // round tp172d, measured on the reference's 1 kHz level staircase (refcap0124): the drive adds 1.72 dB of gain per
    // step (+4.3 / +8.6 / +12.9 / +17.2 dB at 2.5 / 5 / 7.5 / 10) while the output it saturates into falls 1.46 dB per
    // step (a 0 dBFS sine comes out at about 2.4 − 1.46 × drive dB RMS before the wet trim): louder and squarer as it
    // goes, never hotter. A soft clipper with a small bias for the tube's even harmonics (its DC is high-passed after)
    const auto g = (float) std::pow (10.0, 1.72 * amount / 20.0);
    // round trial187b (the trial's sung line, music and sweep at Drive 3 / 6 / 10): the ceiling sinks a little faster
    // at the top (−0.025 dB per step², −2.5 dB at 10), where a whole programme came out 2.8 dB loud
    const auto ceiling = (float) std::pow (10.0, (2.4 - 1.46 * amount - 0.025 * amount * amount) / 20.0);
    constexpr float bias = 0.15f;
    const auto shaped = ceiling * (std::tanh (g * x / ceiling + bias) - std::tanh (bias)) / (1.0f - std::tanh (bias) * std::tanh (bias));
    // crossfade from clean over the first step so that 0 → 0.1 does not jump
    const auto w = (float) std::min (1.0, amount);
    return x + w * (shaped - x);
}

static double dbToGain (double db) noexcept { return std::pow (10.0, db / 20.0); }

double tiltDb (double semitones) noexcept
{
    const auto x = std::clamp (semitones, -36.0, 36.0);
    return x >= 0.0 ? 3.2 * std::tanh (x / 4.0) : 0.43 * x;
}

const EffectInfo& info()
{
    static const EffectInfo i { "Voice Shift",
                                { { "Pitch", 0.5f }, { "Formant", 0.5f }, { "Link", 0.0f }, { "Mode", 0.0f }, { "Drive", 0.0f }, { "Mix", 1.0f },
                                  { "MIDI", 0.0f }, { "Law", 1.0f } } };
    return i;
}

std::string paramText (int param, float v)
{
    char t[32];
    switch (param)
    {
        case pitch:
        case formant: std::snprintf (t, sizeof t, "%+.1f st", pitchSemitones (v)); return t;
        case link: return v >= 0.5f ? "On" : "Off";
        case mode:
        {
            static const char* const n[3] { "Transpose", "Quantize", "Robot" };
            return n[(int) modeOf (v)];
        }
        case drive: std::snprintf (t, sizeof t, "%.1f", driveAmount (v)); return t;
        case mix: std::snprintf (t, sizeof t, "%.0f%%", 100.0 * std::clamp (v, 0.0f, 1.0f)); return t;
        case law: return v >= 0.5f ? "Measured" : "Classic";
        case midi:
        {
            const auto c = midiChannelIndex (v);
            if (c < 0)
                return "Off";
            std::snprintf (t, sizeof t, "Channel %d", c + 1);
            return t;
        }
        default: std::snprintf (t, sizeof t, "%.2f", (double) v); return t;
    }
}

namespace
{
// round trial187b [black-box, the trial's renders of a vocal on the left only / a vocal left and music right / a vocal
// and its 23 ms echo]: the reference shifts each channel on its own (a voice on the left leaves the right silent; the
// left comes out the same whatever plays on the right), so each channel has its own detector and shifter here (they
// shared the mid's marks). With the same voice on both sides its two outputs still differ once anything is shifted:
// the right one runs about 1.25 ms behind (a constant 60 samples at 48 kHz with Formant −3; L / R correlation 1.0 at
// 0 / 0, about 0 when shifted), which keeps a shifted voice wide rather than in the centre
constexpr double rightLagMs = 1.25;

class Shifter final : public Effect
{
public:
    Shifter() : dry ((std::size_t) (2 * dryRing), 0.0f), lagLine ((std::size_t) lagRing, 0.0f) {}

    void prepare (double sampleRate) noexcept override
    {
        sr = std::clamp (sampleRate > 0.0 ? sampleRate : 44100.0, 8000.0, 192000.0);
        hop = std::max (16, (int) std::lround (128.0 * sr / 44100.0));
        latency = std::min (latencyFor (sr), dryRing - 1);
        for (auto& v : voices)
        {
            v.det.prepare (sr, minHz, maxHz, hop);
            v.det.setThreshold (0.15f);
            v.psola.prepare (sr, sr / minHz, latency);
            v.psola.setMarkLag (v.det.windowCentre());
        }
        rightLag = std::clamp ((int) std::lround (rightLagMs * 0.001 * sr), 0, lagRing - 1);
        dcA = (float) std::exp (-2.0 * 3.14159265358979323846 * 10.0 / sr);
        reset();
    }

    void reset() noexcept override
    {
        for (auto& v : voices)
        {
            v.det.reset();
            v.psola.reset();
            v.sung = -1.0;
            v.wanted = v.shift = 0.0;
            v.unvoicedFor = 1e9;
            v.target = -1;
        }
        std::fill (dry.begin(), dry.end(), 0.0f);
        std::fill (lagLine.begin(), lagLine.end(), 0.0f);
        dryPos = 0;
        still = 1.0;
        apart = 0.0;
        dcX[0] = dcX[1] = dcY[0] = dcY[1] = 0.0f;
    }

    void setHeldNotes (const NoteMask* channels, int count) noexcept override
    {
        held = channels;
        heldCount = count;
    }

    void process (float* l, float* r, int n, const EffectParams& p, double) noexcept override
    {
        // round 137: the highest note held on the MIDI channel replaces the Pitch knob (relative to C4) / the robot note
        int midiNote = -1;
        if (const auto ch = midiChannelIndex (p[midi]); ch >= 0 && held != nullptr && ch < heldCount)
            for (int k = 127; k >= 0 && midiNote < 0; --k)
                if ((held[ch][(std::size_t) (k >> 6)] >> (k & 63)) & 1u)
                    midiNote = k;
        meterMidi.store (midiNote < 0 ? 0.0f : (float) midiNote, std::memory_order_relaxed);
        const auto robotAt = midiNote >= 0 ? (double) midiNote : robotNote;
        const auto pitchSt = midiNote >= 0 ? (modeOf (p[mode]) == Mode::robot ? 0.0 : std::clamp ((double) midiNote - robotNote, -24.0, 24.0))
                                           : pitchSemitones (p[pitch]);
        const auto formantSt = formantSemitones (p[formant]);
        const auto linked = p[link] >= 0.5f;
        const auto md = modeOf (p[mode]);
        const auto amount = driveAmount (p[drive]);
        const auto wet = std::clamp (p[mix], 0.0f, 1.0f);
        const bool measured = p[law] >= 0.5f; // round 190dp
        const auto stereo = r != nullptr && r != l;
        const int channels = stereo ? 2 : 1;
        // Quantize: "as quickly as possible" — a 2 ms glide between notes, only to avoid clicks
        const auto glide = 1.0 - std::exp (-1.0 / (0.002 * sr));
        const auto fShift = std::exp2 (formantSt / 12.0);
        // round tp172d slice 11 / trial187b: nothing to shift (Transpose at Pitch 0 / Formant 0, no MIDI note) = the input itself,
        // delayed — the reference nulls its input there within its level trim (−15 dB on a sung line), while the
        // overlap-add left a sung note up to half a period off its input; the switch fades over 20 ms
        const bool unshifted = md == Mode::transpose && midiNote < 0 && std::abs (pitchSt) < 1e-6 && std::abs (formantSt) < 1e-6;
        const auto fade = 1.0 - std::exp (-1.0 / (0.02 * sr));

        for (int i = 0; i < n; ++i)
        {
            auto clean = [] (float v) { return std::isfinite (v) ? v : 0.0f; };
            const float xin[2] { clean (l[i]), clean (stereo ? r[i] : l[i]) };
            float y[2] { 0.0f, 0.0f };
            double shifted = 0.0; // how far the voice is moved (semitones of pitch or formant), for the right lag
            for (int c = 0; c < channels; ++c)
            {
                auto& v = voices[(std::size_t) c];
                v.psola.setUnvoicedShift (measured);
                if (v.det.push (xin[c]))
                {
                    const auto& e = v.det.estimate();
                    if (e.voiced && e.hz > 0.0)
                    {
                        v.psola.setPitch (e.period, true);
                        v.sung = vocal::hzToNote (e.hz);
                        v.unvoicedFor = 0.0;
                        if (md == Mode::quantize)
                        {
                            v.target = vocal::nearestNote (v.sung, 0xfff, v.target, 0.03);
                            v.wanted = (double) v.target - v.sung;
                        }
                        else if (md == Mode::robot)
                        {
                            v.target = (int) robotAt;
                            v.wanted = robotAt - v.sung;
                        }
                        else
                        {
                            v.target = -1;
                            v.wanted = 0.0;
                        }
                    }
                    else
                    {
                        v.psola.setPitch (0.0, false);
                        v.unvoicedFor += (double) hop / sr;
                        if (v.unvoicedFor > 0.06)
                        {
                            v.sung = -1.0;
                            v.target = -1;
                        }
                    }
                }
                v.shift += (md == Mode::transpose ? 1.0 : glide) * (v.wanted - v.shift);
                const auto semis = std::clamp (v.shift + pitchSt, -24.0, 24.0);
                const auto ratio = std::exp2 (semis / 12.0);
                v.psola.setShift (ratio, linked ? ratio * fShift : fShift);
                v.psola.process (&xin[c], &y[c], 1);
                // round tp172d, measured on the reference's Pitch and Formant sweeps (refcap0124): its voice gets louder
                // as the pitch moves up against the formants (and quieter going down), where the overlap-add here keeps
                // the level — the tilt is matched before the drive, which hears it too. Round trial187b (the trial's
                // sung line at Pitch ±12 / +5 / −7, Formant ±6): the rise levels off going up (+2.6 / +3.1 / +3.0 dB
                // at +5 / +6 / +12 st, 3.2 tanh (x / 4)) and keeps falling 0.43 dB per semitone going down (−2.5 /
                // −3.8 / −5.4 dB at −6 / −9 / −12); round tp172d's straight 0.36 dB per semitone was 1.3 dB loud at ±12
                const auto formantTotal = linked ? semis + formantSt : formantSt;
                y[c] *= (float) dbToGain (tiltDb (semis - formantTotal));
                shifted = std::max (shifted, std::abs (semis) + std::abs (formantTotal));
            }

            const auto w = (std::size_t) (dryPos & (dryRing - 1));
            const auto rd = (std::size_t) ((dryPos - latency) & (dryRing - 1));
            dry[w] = xin[0];
            dry[w + dryRing] = xin[1];
            const auto d0 = dry[rd], d1 = dry[rd + dryRing];
            ++dryPos;

            still += ((unshifted ? 1.0 : 0.0) - still) * fade;
            if (still > 1e-6)
            {
                y[0] = (float) (y[0] + still * (d0 - y[0]));
                y[1] = (float) (y[1] + still * (d1 - y[1]));
            }
            if (stereo)
            {
                // the right channel's lag, faded in over the first half semitone of shift
                apart += (std::min (1.0, shifted / 0.5) * (1.0 - still) - apart) * fade;
                lagLine[(std::size_t) (lagPos & (lagRing - 1))] = y[1];
                const auto late = lagLine[(std::size_t) ((lagPos - rightLag) & (lagRing - 1))];
                ++lagPos;
                y[1] = (float) (y[1] + apart * (late - y[1]));
            }
            if (amount > 0.0)
                for (int c = 0; c < channels; ++c)
                {
                    // the tube's asymmetry leaves a DC offset: a 10 Hz high-pass after it
                    const auto sh = driveCurve (y[c], amount);
                    dcY[c] = sh - dcX[c] + dcA * dcY[c];
                    dcX[c] = sh;
                    y[c] = dcY[c];
                }

            // round tp172d: the reference's wet voice sits 2.7 dB under its input (measured at every level)
            constexpr float wetTrim = 0.733f;
            l[i] = d0 + wet * (wetTrim * y[0] - d0);
            if (stereo)
                r[i] = d1 + wet * (wetTrim * y[1] - d1);
        }
        const auto& v0 = voices[0];
        meterSung.store ((float) v0.sung, std::memory_order_relaxed);
        meterOut.store (v0.sung > 0.0 ? (float) (v0.sung + v0.shift + pitchSt) : -1.0f, std::memory_order_relaxed);
    }

    float controlOutput (int o) const noexcept override
    {
        const auto v = (o == 0 ? meterSung : o == 2 ? meterMidi : meterOut).load (std::memory_order_relaxed);
        return std::clamp (v, 0.0f, 127.0f) / 127.0f;
    }

    int latencySamples() const noexcept override { return latency; }

private:
    struct Voice
    {
        vocal::PitchDetector det;
        vocal::Psola psola;
        double sung = -1.0, wanted = 0.0, shift = 0.0, unvoicedFor = 1e9;
        int target = -1;
    };
    static constexpr int dryRing = 16384, lagRing = 1024;
    double sr = 44100.0;
    int hop = 128, latency = 1264, rightLag = 60;
    std::array<Voice, 2> voices;
    std::vector<float> dry, lagLine;
    std::int64_t dryPos = 0, lagPos = 0;
    double still = 1.0, apart = 0.0;
    float dcA = 0.999f, dcX[2] {}, dcY[2] {};
    std::atomic<float> meterSung { -1.0f }, meterOut { -1.0f }, meterMidi { 0.0f };
    const NoteMask* held = nullptr;
    int heldCount = 0;
};
} // namespace

std::unique_ptr<Effect> make()
{
    auto e = std::make_unique<Shifter>();
    e->prepare (44100.0);
    return e;
}
} // namespace lynx::voiceshift
