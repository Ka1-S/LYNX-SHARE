#include "core/VoiceShift.h"

#include <algorithm>
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
    const auto ceiling = (float) std::pow (10.0, (2.4 - 1.46 * amount) / 20.0);
    constexpr float bias = 0.15f;
    const auto shaped = ceiling * (std::tanh (g * x / ceiling + bias) - std::tanh (bias)) / (1.0f - std::tanh (bias) * std::tanh (bias));
    // crossfade from clean over the first step so that 0 → 0.1 does not jump
    const auto w = (float) std::min (1.0, amount);
    return x + w * (shaped - x);
}

const EffectInfo& info()
{
    static const EffectInfo i { "Voice Shift",
                                { { "Pitch", 0.5f }, { "Formant", 0.5f }, { "Link", 0.0f }, { "Mode", 0.0f }, { "Drive", 0.0f }, { "Mix", 1.0f },
                                  { "MIDI", 0.0f } } };
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
class Shifter final : public Effect
{
public:
    Shifter() : dry ((std::size_t) (2 * dryRing), 0.0f) {}

    void prepare (double sampleRate) noexcept override
    {
        sr = std::clamp (sampleRate > 0.0 ? sampleRate : 44100.0, 8000.0, 192000.0);
        hop = std::max (16, (int) std::lround (128.0 * sr / 44100.0));
        det.prepare (sr, minHz, maxHz, hop);
        det.setThreshold (0.15f);
        latency = std::min (latencyFor (sr), dryRing - 1);
        psola.prepare (sr, sr / minHz, latency);
        psola.setMarkLag (det.windowCentre());
        dcA = (float) std::exp (-2.0 * 3.14159265358979323846 * 10.0 / sr);
        passStep = (float) (1.0 - std::exp (-1.0 / (0.020 * sr)));
        reset();
    }

    void reset() noexcept override
    {
        det.reset();
        psola.reset();
        std::fill (dry.begin(), dry.end(), 0.0f);
        dryPos = 0;
        sung = -1.0;
        wanted = shift = 0.0;
        unvoicedFor = 1e9;
        target = -1;
        dcX[0] = dcX[1] = dcY[0] = dcY[1] = 0.0f;
        passGain = 1.0f;
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
        const auto stereo = r != nullptr && r != l;
        // Quantize: "as quickly as possible" — a 2 ms glide between notes, only to avoid clicks
        const auto glide = 1.0 - std::exp (-1.0 / (0.002 * sr));
        const auto fShift = std::exp2 (formantSt / 12.0);

        for (int i = 0; i < n; ++i)
        {
            auto clean = [] (float v) { return std::isfinite (v) ? v : 0.0f; };
            const float xin[2] { clean (l[i]), clean (stereo ? r[i] : l[i]) };
            const auto mid = stereo ? 0.5f * (xin[0] + xin[1]) : xin[0];

            if (det.push (mid))
            {
                const auto& e = det.estimate();
                if (e.voiced && e.hz > 0.0)
                {
                    psola.setPitch (e.period, true);
                    sung = vocal::hzToNote (e.hz);
                    unvoicedFor = 0.0;
                    if (md == Mode::quantize)
                    {
                        target = vocal::nearestNote (sung, 0xfff, target, 0.03);
                        wanted = (double) target - sung;
                    }
                    else if (md == Mode::robot)
                    {
                        target = (int) robotAt;
                        wanted = robotAt - sung;
                    }
                    else
                    {
                        target = -1;
                        wanted = 0.0;
                    }
                }
                else
                {
                    psola.setPitch (0.0, false);
                    unvoicedFor += (double) hop / sr;
                    if (unvoicedFor > 0.06)
                    {
                        sung = -1.0;
                        target = -1;
                    }
                }
            }
            shift += (md == Mode::transpose ? 1.0 : glide) * (wanted - shift);
            const auto semis = std::clamp (shift + pitchSt, -24.0, 24.0);
            const auto ratio = std::exp2 (semis / 12.0);
            psola.setShift (ratio, linked ? ratio * fShift : fShift);

            float y[2];
            psola.process (xin, y, stereo ? 2 : 1);
            {
                // round tp172d slice 11: at no shift the reference passes the voice almost untouched (its default state:
                // a vocal comes out 2.7 dB down, flat within 0.4 dB to 4 kHz, the rest −15.5 dB under it; the overlap-add
                // here left only −2.2 dB, and −0.5 dB on a sweep). So with nothing to shift the wet path is the input,
                // delayed as the overlap-add is, with a 20 ms crossfade whenever shifting starts or stops
                const bool unity = md == Mode::transpose && midiNote < 0 && std::abs (semis) < 1.0e-6
                                   && std::abs ((linked ? ratio * fShift : fShift) - 1.0) < 1.0e-9;
                passGain += ((unity ? 1.0f : 0.0f) - passGain) * passStep;
                if (passGain > 1.0e-6f)
                {
                    const auto rdp = (std::size_t) ((dryPos - latency) & (dryRing - 1));
                    y[0] += passGain * (dry[rdp] - y[0]);
                    y[1] += passGain * ((stereo ? dry[rdp + dryRing] : dry[rdp]) - y[1]);
                }
            }
            {
                // round tp172d, measured on the reference's Pitch and Formant sweeps (refcap0124): its voice gets
                // 0.36 dB louder per semitone the pitch moves up against the formants (and quieter going down), where
                // the overlap-add here keeps the level — the tilt is matched before the drive, which hears it too
                const auto formantTotal = linked ? semis + formantSt : formantSt;
                const auto tilt = (float) std::exp2 (0.36 / 6.0206 * std::clamp (semis - formantTotal, -36.0, 36.0));
                y[0] *= tilt;
                y[1] *= tilt;
            }
            if (amount > 0.0)
                for (int c = 0; c < (stereo ? 2 : 1); ++c)
                {
                    // the tube's asymmetry leaves a DC offset: a 10 Hz high-pass after it
                    const auto s = driveCurve (y[c], amount);
                    dcY[c] = s - dcX[c] + dcA * dcY[c];
                    dcX[c] = s;
                    y[c] = dcY[c];
                }

            const auto w = (std::size_t) (dryPos & (dryRing - 1));
            const auto rd = (std::size_t) ((dryPos - latency) & (dryRing - 1));
            dry[w] = xin[0];
            dry[w + dryRing] = xin[1];
            const auto d0 = dry[rd], d1 = dry[rd + dryRing];
            ++dryPos;
            // round tp172d: the reference's wet voice sits 2.7 dB under its input (measured at every level)
            constexpr float wetTrim = 0.733f;
            l[i] = d0 + wet * (wetTrim * y[0] - d0);
            if (stereo)
                r[i] = d1 + wet * (wetTrim * y[1] - d1);
        }
        meterSung.store ((float) sung, std::memory_order_relaxed);
        meterOut.store (sung > 0.0 ? (float) (sung + shift + pitchSt) : -1.0f, std::memory_order_relaxed);
    }

    float controlOutput (int o) const noexcept override
    {
        const auto v = (o == 0 ? meterSung : o == 2 ? meterMidi : meterOut).load (std::memory_order_relaxed);
        return std::clamp (v, 0.0f, 127.0f) / 127.0f;
    }

    int latencySamples() const noexcept override { return latency; }

private:
    static constexpr int dryRing = 16384;
    double sr = 44100.0;
    int hop = 128, latency = 1264;
    vocal::PitchDetector det;
    vocal::Psola psola;
    std::vector<float> dry;
    std::int64_t dryPos = 0;
    double sung = -1.0, wanted = 0.0, shift = 0.0, unvoicedFor = 1e9;
    int target = -1;
    float dcA = 0.999f, dcX[2] {}, dcY[2] {};
    float passGain = 1.0f, passStep = 0.001f; // round tp172d slice 11: the share of the plain (delayed) input at no shift
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
