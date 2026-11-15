#include "core/EmberClassic.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace lynx::emberclassic
{
namespace
{
#include "core/EmberClassicTables.inc"

constexpr double pi = 3.141592653589793;
constexpr int osFactor = 4;
constexpr int cutHalf = 15;
constexpr int osTaps[2] { 59, 19 };            // N = 4k + 3: the centre index (N − 1) / 2 is odd
constexpr double osBeta[2] { 9.0, 8.5 };

double besselI0 (double x)
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 50; ++k)
    {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
        if (term < 1e-12 * sum)
            break;
    }
    return sum;
}

double dbToGain (double db) noexcept { return std::pow (10.0, db / 20.0); }

// The five styles' constants, fitted on the official trial (REA_rodada136 §1; tmp/vox136/lab). Style A is the one
// every state of her corpus uses; E, N, T, P are fitted the same way on the same signal set (round 138: started from
// A's constants with each style's measured colour FIR; mean null over the stairs, bursts and takes at drive 0 … 6:
// E −16.3, N −16.6, T −12.1, P −12.5 dB, from −8.9 … −9.5 with A's curve; A: −25.2 dB). Round 165: each style's
// measured Punish law (the small-signal gain Punish adds at drive 0 … 10, within 0.03 – 0.14 dB of the trial).
const StyleModel styleTable[styleCount] {
    StyleModel { -0.255358, 3.28943, 9.75, -1.14311, 424.977, -0.326658, 1090.62, -0.367324, 0.539481, 1.51943, 1.53834, 2.58446, -0.356477, -1.10167, 69.6495, 124.432, 16.5416,
                 { -0.050348522, 0.45404479, 0.79960583, -0.25489203, 0.00071080674, 0.13172658, -0.18265066, 0.1758486, -0.13859833, 0.089003814, -0.046502489, 0.012880354, 0.0057738622, -0.013574345, 0.012208355, -0.0082160702 }, 13.830, 0.4057 },
    StyleModel { -1.75235, 3.28943, 9.75, 1.11786, 1115.83, 0.0747911, 9922.27, 2.51661, 0.451642, 7.54995, 0.882348, 7.96975, -0.172261, -1.54865, 64.4605, 53.2492, 8.13625,
                 { -0.10601218, 0.88559837, 0.52104064, -0.59799496, 0.51338433, -0.44356996, 0.3946877, -0.32920235, 0.27590921, -0.21574306, 0.16618424, -0.11710939, 0.082003896, -0.04956957, 0.029232918, -0.011986428 }, 14.012, 0.4787 }, // E
    StyleModel { -0.350645, 3.28943, 9.75, -1.63499, 632.752, 1.62653, 1283.86, 0.729425, 0.742618, 5.08067, 1.10494, 5.74673, -0.154669, -1.96319, 28.2141, 36.5461, 13.2041,
                 { -0.057462716, 0.51475856, 0.59565603, -0.1951887, 0.20100539, -0.16211082, 0.1583298, -0.12293189, 0.11337933, -0.08247574, 0.070085289, -0.045254, 0.03620832, -0.01890969, 0.013629928, -0.0037943876 }, 14.139, 0.1029 }, // N
    StyleModel { -0.0156997, 3.28943, 9.75, -0.875387, 42.6041, -2.6432, 5406.14, 2.39267, 0.237349, 2.3023, 1.07218, 4.50339, -0.380718, -2.98134, 127.063, 102.321, 25.3318,
                 { -0.095832879, 0.81800219, 0.58278911, -0.55857331, 0.43114637, -0.35812354, 0.31761663, -0.26122303, 0.22193496, -0.17084589, 0.13440571, -0.092138368, 0.067173398, -0.038256847, 0.024569006, -0.0080880511 }, 14.017, 0.5118 }, // T
    StyleModel { 0.703021, 3.28943, 9.75, -0.757538, 174.47, 4.1792, 1155.66, 1.21455, 0.203818, 3.41136, 1.40577, 2.1345, -0.476152, -2.62538, 94.3472, 69.8909, 39.9236,
                 { -0.07587156, 0.67775215, 0.59516886, -0.39260415, 0.32049256, -0.26406084, 0.24365413, -0.19383356, 0.17184911, -0.12649202, 0.10493743, -0.067138583, 0.053183781, -0.026498863, 0.020158807, -0.0039552972 }, 13.924, 0.7414 }, // P
};

// analog biquad (b2 s² + b1 s + b0) / (a2 s² + a1 s + a0), s = j·f in Hz → digital by the bilinear transform
// prewarped at `hz`
void bilinear (Biquad& f, double sr, double hz, double b2, double b1, double b0, double a2, double a1, double a0) noexcept
{
    const double k = hz / std::tan (pi * hz / sr); // s = k (1 − z⁻¹) / (1 + z⁻¹)
    const double k2 = k * k;
    f.set (b2 * k2 + b1 * k + b0, 2.0 * (b0 - b2 * k2), b2 * k2 - b1 * k + b0,
           a2 * k2 + a1 * k + a0, 2.0 * (a0 - a2 * k2), a2 * k2 - a1 * k + a0);
}

// first-order bilinear shelf: gain `db` below (low) or above (high) `hz`, unity on the other side
template <typename P>
void shelf1 (P& f, double sr, double hz, double db, bool high) noexcept
{
    const auto g = dbToGain (db);
    const auto k = std::tan (pi * std::min (hz, sr * 0.45) / sr);
    double b0, b1;
    if (high) { b0 = g + k; b1 = k - g; }
    else { b0 = 1.0 + g * k; b1 = g * k - 1.0; }
    const double a0 = 1.0 + k, a1 = k - 1.0;
    f.b0 = b0 / a0; f.b1 = b1 / a0; f.a1 = a1 / a0;
}
// round 165 [EXEC trial, REA_rodada165ha §2]: style A with Punish on drives its curve differently (harder negative
// knee, less dynamic bias) — fitted on the trial's renders at drives 1 / 3 / 6 / 10 (stairs, bursts, two takes): mean
// null −10.0 dB with A's own curve → −17.4 dB. The other styles keep their own curve under Punish (not measured yet).
StyleModel punishModelA()
{
    auto m = styleTable[0];
    m.punishLawDb = 13.7233;
    m.punishLawPerUnitDb = 0.346217;
    m.outDb = -0.888831;
    m.cPos = 0.471376;
    m.pPos = 2.69529;
    m.cNeg = 1.11787;
    m.pNeg = 6.67158;
    m.bias = -0.166177;
    m.biasDyn = -0.249233;
    m.envAttackMs = 130.526;
    m.envReleaseMs = 40.9654;
    return m;
}
// round trial187b [EXEC trial, black-box]: the styles at high drive. The table above was fitted at drives 0 … 6; at 8 / 10
// the trial came out quieter than it and with a softer knee (programme level error before: A +0.8, E +5.1, T / P +2.4,
// E with Punish +4.4 dB). A "hot" model per style (and per style with Punish), fitted on the trial's renders at drives
// 8 / 9 / 10 (sweep, stair, drums, music, vocal; Tone, Mix, Low cut, Thump and Steep variants; round tp172d's E 6 / N 8
// takes) with trial187b/fit/emberfit (coordinate descent on the null plus twice the level error, knees ≥ 0.05): the
// curve blends from the table at drive 5 to it at drive 10, and its output trim keeps the table's small-signal gain, so
// round 136 / 165's drive and Punish laws (measured at −70 dBFS) are unchanged. Level error after: within 0.3 dB (E
// with Punish −1.0); fxparity on those takes 3.1 → 3.8, round tp172d's 4.8 → 5.0. Below drive 5 nothing changes.
struct HotModel { double outDb, cPos, pPos, cNeg, pNeg, bias, biasDyn, envAttackMs, envReleaseMs, preLowDb, preHighDb, servo = 0.0; };
constexpr double servoMax = 256.0; // the coupling's bias, in the curve's units (drive 10 with Punish at full scale: about 180)
constexpr double hotFromDrive = 5.0;
constexpr HotModel hotTable[styleCount] {
    { -1.1431, 0.3616, 3.6867, 1.5383, 2.2379, -0.3772, -1.0326, 69.650, 124.432, 0.257, 0.721 }, // A
    { 1.1179, 0.0686, 1.5908, 0.3965, 1.9849, -0.1363, -2.1967, 55.816, 53.249, -1.180, 7.560 }, // E
    { -1.6350, 0.7426, 1.2038, 1.1049, 1.7309, -0.0687, -1.1247, 21.276, 36.546, 0.827, 0.557 }, // N
    { -0.8754, 0.1859, 0.8000, 0.8300, 3.0187, -0.2836, -7.3109, 76.981, 6.906, -2.220, 0.793 }, // T
    { -0.7575, 0.0500, 0.9239, 0.9423, 3.1843, -0.4272, -1.6654, 77.567, 69.891, 0.223, 2.667 }, // P
};
constexpr HotModel hotPunishTable[styleCount] {
    { -0.8888, 0.4714, 1.9697, 1.1179, 2.0146, -0.1238, -0.1292, 130.526, 40.965, -0.327, -0.079 }, // A
    { 1.1179, 0.0500, 7.5499, 0.2492, 11.8895, -0.0689, -2.9887, 64.460, 53.249, 1.163, 0.444 }, // E
    { -1.6350, 0.4480, 3.9332, 0.9944, 1.7588, -0.1014, -1.4262, 22.194, 36.546, 1.730, 1.382 }, // N
    { -0.8754, 0.2302, 0.9660, 0.6969, 1.9523, -0.2115, -6.8423, 127.063, 102.321, -1.284, 2.496 }, // T
    { -0.7575, 0.0636, 3.4114, 1.2090, 2.9207, -0.2295, -5.1214, 94.347, 69.891, 0.179, 2.199 }, // P
};
// round 190ar [EXEC trial, black-box]: the high-drive tables fitted again with the coupling ahead of the curve
// (StyleModel::servo). On a 1 kHz tone at drive 10 the reference's square wave is uneven in styles E, T and P: the
// lower ceiling lasts 67 … 77 % of the period from −21 dBFS up (+0.33 / −0.85 in T, +0.32 / −0.85 in P) and its second
// harmonic stays near −10 dB, where round trial187b's tables went even (±0.42, second harmonic −41 dB at −3 dBFS).
// Fitted with parity190ar/ember/emfit3 (coordinate descent from a zero static bias: the null plus twice the level error
// on round trial187b's takes and round trial190k's stairs), then both ceilings trimmed (T −0.7, P −0.3 dB) so that no
// take's level moves over a score step. Fit error T −3.2 → −6.2, P −2.0 → −5.4, P with Punish −4.8 → −5.9 dB; fxparity
// on the 100 high-drive takes 3.76 → 3.91 (12 up, none down), their null −7.5 → −8.3 dB. Styles A and N, T and E with
// Punish and E keep round trial187b's rows: A and N had no gain, the other three each put one take a score step down.
constexpr HotModel hotTable190[styleCount] {
    { -1.1431, 0.3616, 3.6867, 1.5383, 2.2379, -0.3772, -1.0326, 69.650, 124.432, 0.257, 0.721, 0.00000 }, // A
    { 1.1179, 0.0686, 1.5908, 0.3965, 1.9849, -0.1363, -2.1967, 55.816, 53.249, -1.180, 7.560, 0.00000 }, // E
    { -1.6350, 0.7426, 1.2038, 1.1049, 1.7309, -0.0687, -1.1247, 21.276, 36.546, 0.827, 0.557, 0.00000 }, // N
    { -0.8754, 0.3321, 1.0334, 0.5689, 1.0842, 0.0600, -20.6108, 381.289, 1.394, -2.156, 2.393, 0.23599 }, // T
    { -0.7575, 0.3574, 1.4231, 0.9564, 1.3208, 0.0600, -12.7504, 233.958, 1.763, 0.223, 0.779, 0.66766 }, // P
};
constexpr HotModel hotPunishTable190[styleCount] {
    { -0.8888, 0.4714, 1.9697, 1.1179, 2.0146, -0.1238, -0.1292, 130.526, 40.965, -0.327, -0.079, 0.00000 }, // A
    { 1.1179, 0.0500, 7.5499, 0.2492, 11.8895, -0.0689, -2.9887, 64.460, 53.249, 1.163, 0.444, 0.00000 }, // E
    { -1.6350, 0.4480, 3.9332, 0.9944, 1.7588, -0.1014, -1.4262, 22.194, 36.546, 1.730, 1.382, 0.00000 }, // N
    { -0.8754, 0.2302, 0.9660, 0.6969, 1.9523, -0.2115, -6.8423, 127.063, 102.321, -1.284, 2.496, 0.00000 }, // T
    { -0.7575, 0.5520, 6.8267, 0.6636, 4.7504, 0.0816, -6.6904, 73.932, 1.763, -0.577, 1.579, 0.29524 }, // P
};

StyleModel hotBlend (const StyleModel& b, const HotModel& h, double drive) noexcept
{
    const double w = std::clamp ((drive - hotFromDrive) / (10.0 - hotFromDrive), 0.0, 1.0);
    if (w <= 0.0)
        return b;
    auto m = b;
    const auto mix = [w] (double x, double y) { return x + w * (y - x); };
    m.outDb = mix (b.outDb, h.outDb);
    m.cPos = mix (b.cPos, h.cPos);
    m.pPos = mix (b.pPos, h.pPos);
    m.cNeg = mix (b.cNeg, h.cNeg);
    m.pNeg = mix (b.pNeg, h.pNeg);
    m.bias = mix (b.bias, h.bias);
    m.biasDyn = mix (b.biasDyn, h.biasDyn);
    m.envAttackMs = mix (b.envAttackMs, h.envAttackMs);
    m.envReleaseMs = mix (b.envReleaseMs, h.envReleaseMs);
    m.preLowDb = mix (b.preLowDb, h.preLowDb);
    m.preHighDb = mix (b.preHighDb, h.preHighDb);
    m.servo = mix (b.servo, h.servo);
    // the small-signal gain stays the table's (the measured drive / Punish laws): the output trim makes up the curve's
    // slope at its bias, side'(b) = (1 + |b / c|^p)^(−(1 + p) / p)
    const auto slopeDb = [] (const StyleModel& q) {
        const double c = q.bias >= 0.0 ? q.cPos : q.cNeg, p = q.bias >= 0.0 ? q.pPos : q.pNeg;
        return 20.0 * std::log10 (std::pow (1.0 + std::pow (std::abs (q.bias / c), p), -(1.0 + p) / p));
    };
    m.outDb += (b.outDb + slopeDb (b)) - (m.outDb + slopeDb (m));
    return m;
}
} // namespace

const StyleModel& styleModel (int style) noexcept { return styleTable[std::clamp (style, 0, styleCount - 1)]; }

const StyleModel& punishModel (int style) noexcept
{
    // round 172d [black-box, the trial with Auto off and Output at 0 dB, music and stairs at drives 2 … 8]: E / N / T / P
    // with Punish on saturate much lower than their own curve under the +14 dB law (loud music 1 … 8 dB quieter, T alike
    // at drives 2 / 4 / 8): their curve scales down (knees and bias alike, so the small-signal law above is unchanged)
    static const std::array<StyleModel, styleCount> models = []
    {
        std::array<StyleModel, styleCount> x;
        constexpr double knee[styleCount] = { 1.0, 0.40, 0.90, 0.65, 0.86 };
        for (int i = 0; i < styleCount; ++i)
        {
            x[(size_t) i] = i == 0 ? punishModelA() : styleModel (i);
            x[(size_t) i].cPos *= knee[i];
            x[(size_t) i].cNeg *= knee[i];
            x[(size_t) i].bias *= knee[i]; // the whole curve scales (its envelope too, so biasDyn stays): the slope
                                           // at the bias, the small-signal gain, is the same
        }
        return x;
    }();
    return models[(size_t) std::clamp (style, 0, styleCount - 1)];
}

// [EXEC] the trial's own value → text laws (VST3 parameter read-back, REA_rodada136 §1)
double driveUnits (float v) noexcept { return 10.0 * std::clamp ((double) v, 0.0, 1.0); }
double toneDb (float v) noexcept { return -12.0 + 24.0 * std::clamp ((double) v, 0.0, 1.0); }
double lowCutHz (float v) noexcept { return 20.0 * std::pow (50.0, std::clamp ((double) v, 0.0, 1.0)); }
double highCutHz (float v) noexcept { return 1000.0 * std::pow (20.0, std::clamp ((double) v, 0.0, 1.0)); }
double outputDb (float v) noexcept { return -24.0 + 24.0 * std::clamp ((double) v, 0.0, 1.0); }
int styleOf (float v) noexcept { return std::clamp ((int) std::lround (std::clamp (v, 0.0f, 1.0f) * 4.0f), 0, styleCount - 1); }

float autoOutput (float output, float driveFrom, float driveTo) noexcept
{
    const auto db = outputDb (output) + autoDbPerUnit * (driveUnits (driveTo) - driveUnits (driveFrom));
    return (float) std::clamp ((db + 24.0) / 24.0, 0.0, 1.0);
}

// ---- the ×4 oversampler (same half-band polyphase scheme as mixsuite::Oversampler, shorter filters)
Engine::Os4::Os4()
{
    for (int k = 0; k < 2; ++k)
    {
        auto& stage = st[(std::size_t) k];
        const int n = osTaps[k], c = (n - 1) / 2;
        const auto i0b = besselI0 (osBeta[k]);
        double sum = 0.0;
        std::array<double, 32> h {};
        for (int j = 0; 2 * j < n; ++j)
        {
            const int t = 2 * j - c;
            const auto r = (double) t / c;
            const auto w = besselI0 (osBeta[k] * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0b;
            h[(std::size_t) j] = std::sin (0.5 * pi * t) / (0.5 * pi * t) * w;
            sum += h[(std::size_t) j];
        }
        stage.taps = (n + 1) / 2;
        for (int j = 0; j < stage.taps; ++j)
            stage.coef[(std::size_t) j] = (float) (h[(std::size_t) j] * 0.5 / sum);
        stage.centreDelay = (c - 1) / 2;
    }
}

void Engine::Os4::reset() noexcept
{
    for (auto& s : st)
    {
        s.upHist.fill (0.0f);
        s.evenHist.fill (0.0f);
        s.oddHist.fill (0.0f);
        s.upPos = s.downPos = 0;
    }
    pad.fill (0.0f);
    padPos = 0;
}

// raw path: stage 1 (59 taps) 29 base samples, stage 2 (19 taps) 9 samples at 2× = 4.5 → 33.5, plus the half sample
// of the 4× → base alignment: 34 [EXEC: impulse test in tests/VoiceMatch136Tests.inc]
int Engine::Os4::latency() noexcept { return 34; }

void Engine::Os4::up (Stage& s, float x, float& a, float& b) noexcept
{
    const int L = s.taps;
    s.upPos = s.upPos == 0 ? L - 1 : s.upPos - 1;
    s.upHist[(std::size_t) s.upPos] = s.upHist[(std::size_t) (s.upPos + L)] = x;
    const float* hist = s.upHist.data() + s.upPos;
    float acc = 0.0f;
    for (int j = 0; j < L; ++j)
        acc += s.coef[(std::size_t) j] * hist[j];
    a = 2.0f * acc;
    b = hist[s.centreDelay];
}

float Engine::Os4::down (Stage& s, float a, float b) noexcept
{
    const int L = s.taps;
    s.downPos = s.downPos == 0 ? L - 1 : s.downPos - 1;
    s.evenHist[(std::size_t) s.downPos] = s.evenHist[(std::size_t) (s.downPos + L)] = a;
    s.oddHist[(std::size_t) s.downPos] = s.oddHist[(std::size_t) (s.downPos + L)] = b;
    const float* even = s.evenHist.data() + s.downPos;
    float acc = 0.0f;
    for (int j = 0; j < L; ++j)
        acc += s.coef[(std::size_t) j] * even[j];
    return acc + 0.5f * s.oddHist[(std::size_t) (s.downPos + s.centreDelay + 1)];
}

int Engine::latency() noexcept { return mixsuite::Oversampler::latency(); }

void Engine::prepare (double sampleRate)
{
    sr = sampleRate > 0 ? sampleRate : 48000.0;
    wet.assign ((size_t) maxBlock, 0.0f);
    for (auto& c : ch)
    {
        c.dry.assign ((size_t) latency(), 0.0f);
        c.wetPad.assign ((size_t) std::max (1, latency() - Os4::latency() - cutHalf), 0.0f);
    }
    design();
    reset();
}

void Engine::reset() noexcept
{
    lowCut.reset();
    tone.reset();
    for (auto* p : { &preLow, &preHigh, &postLow, &postHigh }) p->reset();
    for (auto& c : ch)
    {
        c.dcX = c.dcY = 0.0;
        c.env = 0.0;
        c.servo = 0.0;
        c.os.reset();
        std::fill (c.dry.begin(), c.dry.end(), 0.0f);
        std::fill (c.wetPad.begin(), c.wetPad.end(), 0.0f);
        c.padPos = 0;
        c.styleHist.fill (0.0f);
        c.cutHist.fill (0.0f);
        c.dryPos = c.stylePos = c.cutPos = 0;
    }
}

void Engine::set (const Settings& settings, const StyleModel* model)
{
    s = settings;
    if (model != nullptr)
        m = *model;
    else
    {
        const bool p = s.punish && s.measuredPunish;
        const int st = std::clamp (s.style, 0, styleCount - 1);
        const auto& hot = s.hotCoupling ? (p ? hotPunishTable190 : hotTable190)[st] : (p ? hotPunishTable : hotTable)[st];
        m = hotBlend (p ? punishModel (st) : styleModel (st), hot, s.drive);
    }
    design();
}

void Engine::design()
{
    // [EXEC] Low cut: a 2nd-order high-pass at the knob's frequency, Q 0.847; Thump raises its Q to 1.51 (the bump
    // sits at the corner). Max error 0.1–0.4 dB over 20 Hz … 1 kHz (REA_rodada136 §1.2).
    const double fc = std::clamp (s.lowCutHz, 10.0, sr * 0.4), q = s.thump ? 1.5133 : 0.8466;
    bilinear (lowCut, sr, fc, 1.0 / (fc * fc), 0.0, 0.0, 1.0 / (fc * fc), 1.0 / (q * fc), 1.0);
    // [EXEC] Tone: a 2nd-order tilt: poles fixed (494.6 Hz, Q 0.352), zeros at 494.6 · 10^(−0.9946 t / 20) Hz, the
    // highs at +0.3935 t dB (t > 0) / +1.5885 t dB (t < 0); within 0.25 dB over ±12 dB
    {
        const double t = std::clamp (s.toneDb, -12.0, 12.0), fp = 494.569, qq = 0.3520;
        const double fz = fp * std::pow (10.0, -t * 0.9946 / 20.0);
        const double g = dbToGain ((t > 0 ? 0.3935 : 1.5885) * t);
        bilinear (tone, sr, 1000.0, g, g * fz / qq, g * fz * fz, 1.0, fp / qq, fp * fp);
    }

    shelf1 (preLow, sr, m.preLowHz, m.preLowDb, false);
    shelf1 (preHigh, sr, m.preHighHz, m.preHighDb, true);
    shelf1 (postLow, sr, m.preLowHz, -m.preLowDb, false);
    shelf1 (postHigh, sr, m.preHighHz, -m.preHighDb, true);

    // [EXEC] High cut: the measured 31-tap linear-phase FIR, interpolated between the grid points of the knob
    {
        const double v = std::clamp (std::log (std::clamp (s.highCutHz, 1000.0, 20000.0) / 1000.0) / std::log (20.0), 0.0, 1.0)
                         * (highCutGrid - 1);
        const int k0 = std::min ((int) v, highCutGrid - 2);
        const double a = v - k0;
        const auto& t0 = highCutTaps[s.steep ? 1 : 0][k0];
        const auto& t1 = highCutTaps[s.steep ? 1 : 0][k0 + 1];
        for (int i = 0; i <= cutHalf; ++i)
        {
            const auto c = (float) ((1.0 - a) * t0[i] + a * t1[i]);
            cut[(size_t) (cutHalf + i)] = c;
            cut[(size_t) (cutHalf - i)] = c;
        }
    }

    {
        const double mi = std::clamp (s.mix, 0.0, 1.0) * (dryMixSteps - 1);
        const double oi = std::clamp (-s.outputDb, 0.0, 24.0) / 2.0;
        const int m0 = std::min ((int) mi, dryMixSteps - 2), o0 = std::min ((int) oi, dryOutSteps - 2);
        const double fm = mi - m0, fo = oi - o0;
        const double db = (1 - fm) * ((1 - fo) * dryGainDb[m0][o0] + fo * dryGainDb[m0][o0 + 1])
                          + fm * ((1 - fo) * dryGainDb[m0 + 1][o0] + fo * dryGainDb[m0 + 1][o0 + 1]);
        dryGain = dbToGain (db);
    }
    const double osr = sr * osFactor; // the curve (and the bias envelope) runs at 4×
    const double drive = std::clamp (s.drive, 0.0, 10.0);
    const double punish = ! s.punish ? 0.0 : s.measuredPunish ? m.punishLawDb + m.punishLawPerUnitDb * drive : m.punishDb;
    gain = dbToGain (m.gain0Db + m.gainPerUnitDb * drive + punish);
    out = dbToGain (m.outDb);
    dcA = std::exp (-2.0 * pi * m.dcHz / sr);
    envA = std::exp (-1.0 / (std::max (m.envAttackMs, 0.01) * 0.001 * osr));
    envR = std::exp (-1.0 / (std::max (m.envReleaseMs, 0.01) * 0.001 * osr));
    // round 190ar: the coupling's integrator (per sample at the curve's rate), its leak and the curve's output at rest
    servoK = std::max (m.servo, 0.0) * 1000.0 / osr;
    servoLeak = 1000.0 / (std::max (m.servoLeakMs, 1.0) * osr);
    {
        const double c = m.bias >= 0.0 ? m.cPos : m.cNeg, p = m.bias >= 0.0 ? m.pPos : m.pNeg;
        servoRest = m.bias / std::pow (1.0 + std::pow (std::abs (m.bias / c), p), 1.0 / p);
    }
}

float Engine::curve (float v, double& env, double& servo) const noexcept
{
    // the bias follows the curve's own low-passed output (a coupling capacitor charged by the asymmetric current:
    // "blocking" distortion), with separate charge / discharge times [model fitted to the trial, §1.3]
    const double b = m.bias + m.biasDyn * env - servo; // (servo stays 0 without the coupling)
    auto side = [this] (double x) noexcept
    {
        const double c = x >= 0.0 ? m.cPos : m.cNeg, p = x >= 0.0 ? m.pPos : m.pNeg;
        return x / std::pow (1.0 + std::pow (std::abs (x / c), p), 1.0 / p);
    };
    const double z = side ((double) v + b);
    const double y = z - side (b);
    env = y + (y > env ? envA : envR) * (env - y);
    if (servoK > 0.0) // round 190ar: the coupling charges with the curve's output (bounded: a stuck input cannot run it away)
        servo = std::clamp (servo + servoK * (z - servoRest) - servoLeak * servo, -servoMax, servoMax);
    return (float) y;
}

void Engine::process (float* left, float* right, int n) noexcept
{
    for (int off = 0; off < n; off += maxBlock)
        block (left != nullptr ? left + off : nullptr, right != nullptr ? right + off : nullptr, std::min (maxBlock, n - off));
}

void Engine::block (float* left, float* right, int n) noexcept
{
    float* io[2] { left, right };
    const int lat = latency();
    // [EXEC] the Output knob scales the wet path; the dry part of the Mix follows it partly (dryGainDb, measured)
    const double trim = dbToGain (s.outputDb), wetMix = s.mix * trim, dryMix = (1.0 - s.mix) * dryGain;
    for (int c = 0; c < 2; ++c)
    {
        if (io[c] == nullptr) continue;
        auto& k = ch[(size_t) c];
        float* x = io[c];
        for (int i = 0; i < n; ++i)
        {
            float v = lowCut.process (x[i], c);
            v = tone.process (v, c);
            v = preLow.process (v, c);
            v = preHigh.process (v, c);
            wet[(size_t) i] = (float) (v * gain);
        }
        auto f = [this, &k] (float v) noexcept { return curve (v, k.env, k.servo); };
        k.os.process (wet.data(), n, f); // (round 190gp: block by block, the same samples as run() on each)
        for (int i = 0; i < n; ++i)
        {
            float v = postLow.process (wet[(size_t) i], c);
            v = postHigh.process (v, c);
            const double y0 = v - k.dcX + dcA * k.dcY; // DC blocker
            k.dcX = v;
            k.dcY = y0;
            // pad to Ember's latency (the oversampler and the High cut take less)
            const float y = k.wetPad[(size_t) k.padPos];
            k.wetPad[(size_t) k.padPos] = (float) y0;
            k.padPos = (k.padPos + 1) % (int) k.wetPad.size();
            // style FIR (causal) then the High cut FIR (centre 15)
            k.styleHist[(size_t) k.stylePos] = (float) y;
            double acc = 0.0;
            for (int j = 0; j < styleFirTaps; ++j) acc += m.fir[(size_t) j] * k.styleHist[(size_t) ((k.stylePos - j) & 63)];
            k.stylePos = (k.stylePos + 1) & 63;
            k.cutHist[(size_t) k.cutPos] = (float) acc;
            double w = 0.0;
            for (int j = 0; j < 2 * cutHalf + 1; ++j) w += cut[(size_t) j] * k.cutHist[(size_t) ((k.cutPos - j) & 63)];
            k.cutPos = (k.cutPos + 1) & 63;
            // dry: the input delayed by the wet path's latency
            const float d = k.dry[(size_t) k.dryPos];
            k.dry[(size_t) k.dryPos] = x[i];
            k.dryPos = (k.dryPos + 1) % lat;
            x[i] = (float) (wetMix * w * out + dryMix * d);
        }
    }
}
} // namespace lynx::emberclassic
