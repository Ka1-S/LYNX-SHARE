#include "core/MaximusFx.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>

namespace lynx::maximusfx
{
namespace
{
constexpr std::uint8_t magic[4] { 'L', 'X', 'M', 'X' };
constexpr std::size_t headerSize = 4 + 1 + 1 + 4;
} // namespace

std::vector<std::uint8_t> pack (const Blob& blob)
{
    std::vector<std::uint8_t> out (magic, magic + 4);
    out.push_back (1);
    out.push_back (blob.build == hlfx::MaximusBuild::soundgoodizer ? 1 : 0);
    const auto mix = (std::int32_t) blob.mix;
    std::uint8_t m[4];
    std::memcpy (m, &mix, 4);
    out.insert (out.end(), m, m + 4);
    out.insert (out.end(), blob.state.begin(), blob.state.end());
    return out;
}

std::optional<Blob> unpack (const std::vector<std::uint8_t>& bytes)
{
    if (bytes.size() < headerSize || std::memcmp (bytes.data(), magic, 4) != 0 || bytes[4] != 1 || bytes[5] > 1)
        return std::nullopt;
    Blob b;
    b.build = bytes[5] == 1 ? hlfx::MaximusBuild::soundgoodizer : hlfx::MaximusBuild::shared;
    std::int32_t mix = 0;
    std::memcpy (&mix, bytes.data() + 6, 4);
    b.mix = mix;
    b.state.assign (bytes.begin() + (std::ptrdiff_t) headerSize, bytes.end());
    return b;
}

std::shared_ptr<const hlfx::MaximusPreset> presetOf (const Blob& blob)
{
    return hlfx::makeMaximusPreset (blob.state, blob.build);
}

std::optional<SoundgoodizerState> parseSoundgoodizer (const std::vector<std::uint8_t>& state)
{
    // FUN_00836960: u32 version (> 0), then the preset index and the amount, read raw into the plugin
    if (state.size() < 12)
        return std::nullopt;
    std::int32_t v[3];
    std::memcpy (v, state.data(), sizeof (v));
    if (v[0] < 1 || v[1] < 0 || v[1] > 3)
        return std::nullopt;
    return SoundgoodizerState { v[1], v[2] };
}

const char* soundgoodizerPresetFile (int preset)
{
    static const char* names[4] { "a.dat", "b.dat", "c.dat", "d.dat" };
    return names[preset < 0 || preset > 3 ? 0 : preset];
}

namespace
{
class MaximusEffect final : public Effect
{
public:
    explicit MaximusEffect (const std::vector<std::uint8_t>& slotState)
    {
        if (const auto blob = unpack (slotState))
            if ((preset = presetOf (*blob)) != nullptr)
                mix = blob->mix >= 0 ? blob->mix : hlfx::maximusMixParameter (*preset);
    }

    void prepare (double sampleRate) noexcept override
    {
        if (preset != nullptr)
            engine.prepare (preset, sampleRate, mix); // allocates; called off the audio thread by the host
    }

    void reset() noexcept override { engine.reset(); }

    void process (float* left, float* right, int n, const EffectParams&, double) noexcept override
    {
        engine.process (left, right, n);
    }

    int latencySamples() const noexcept override { return engine.latencySamples(); }

private:
    std::shared_ptr<const hlfx::MaximusPreset> preset;
    int mix = 1000;
    hlfx::MaximusProcessor engine;
};
} // namespace

std::unique_ptr<Effect> make (const std::vector<std::uint8_t>& slotState)
{
    return std::make_unique<MaximusEffect> (slotState);
}
// --- editing -----------------------------------------------------------------------------------------------

const std::vector<StateParam>& stateParams()
{
    // REA_rodada8 §1.2 (FUN_00842440 splits the index; ranges and defaults from the parameter texts and the reset state)
    static const std::vector<StateParam> table = [] {
        std::vector<StateParam> t {
            { "Low band frequency", 0, 65536, 19800 }, { "Low band slope", 0, 1, 1 },
            { "High band frequency", 0, 65536, 48065 }, { "High band slope", 0, 1, 1 },
            { "LMH lookahead delay", 0, 10000, 1115 }, { "LMH mix", 0, 1000, 1000 },
            { "Low cut", -1, 65536, 32768 },
        };
        static const char* bands[4] { "Low", "Mid", "High", "Master" };
        static const char* controls[13] { "mode", "pre gain", "post gain", "stereo separation", "saturation threshold",
                                          "saturation ceiling", "attack", "release 2", "attack curve", "release",
                                          "release curve", "sustain", "envelope mode" };
        static const int ranges[13][3] { { 0, 3, 0 }, { 0, 2000, 1000 }, { 0, 2000, 1000 }, { -1000, 1000, 0 }, { -1000, 1000, 0 },
                                         { 0, 2000, 1000 }, { 0, 10000, 1115 }, { 0, 10000, 0 }, { 1, 8, 2 }, { 0, 10000, 5000 },
                                         { 1, 8, 3 }, { 0, 10000, 2882 }, { 0, 1, 0 } };
        static std::vector<std::string> names; // stable storage for the composed names
        names.reserve (52);
        for (int b = 0; b < 4; ++b)
            for (int j = 0; j < 13; ++j)
                names.push_back (std::string (bands[b]) + " " + controls[j]);
        for (int k = 0; k < 52; ++k)
            t.push_back ({ names[(std::size_t) k].c_str(), ranges[k % 13][0], ranges[k % 13][1], ranges[k % 13][2] });
        return t;
    }();
    return table;
}

int stateParamIndex (const char* name)
{
    if (name == nullptr)
        return -1;
    const auto& t = stateParams();
    for (int i = 0; i < (int) t.size(); ++i)
        if (std::strcmp (t[(std::size_t) i].name, name) == 0)
            return i;
    return -1;
}

std::optional<int> stateParam (const std::vector<std::uint8_t>& slotState, int index)
{
    const auto blob = unpack (slotState);
    if (! blob || index < 0 || index >= 59 || blob->state.size() < 4 + 4 * (std::size_t) (index + 1))
        return std::nullopt;
    if (index == 5 && blob->mix >= 0)
        return blob->mix;
    std::int32_t v = 0;
    std::memcpy (&v, blob->state.data() + 4 + 4 * (std::size_t) index, 4);
    return (int) v;
}

std::vector<std::uint8_t> withStateParam (const std::vector<std::uint8_t>& slotState, int index, int raw)
{
    auto blob = unpack (slotState);
    if (! blob || index < 0 || index >= 59)
        return slotState;
    const auto& p = stateParams()[(std::size_t) index];
    raw = std::max (p.minimum, std::min (p.maximum, raw));
    blob->state = hlfx::withMaximusParams (std::move (blob->state), { { index, raw } });
    if (index == 5 && blob->mix >= 0)
        blob->mix = raw;
    return pack (*blob);
}

namespace
{
// time law T(v) = 2·(501^(v/10000) − 1) ms (REA_rodada36 §2.1)
double timeMs (int v) { return 2.0 * (std::pow (501.0, (double) v / 10000.0) - 1.0); }
// L7 gain law (7^x − 1)/6, x = v/1000
std::string gainText (int v)
{
    const auto g = (std::pow (7.0, (double) v / 1000.0) - 1.0) / 6.0;
    if (g <= 0.0)
        return "-inf dB";
    char t[32];
    std::snprintf (t, sizeof (t), "%+.1f dB", 20.0 * std::log10 (g));
    return t;
}
std::string msText (double ms)
{
    char t[32];
    std::snprintf (t, sizeof (t), ms < 10.0 ? "%.2f ms" : ms < 100.0 ? "%.1f ms" : "%.0f ms", ms);
    return t;
}
} // namespace

std::string stateParamText (int index, int raw)
{
    char t[48];
    switch (index)
    {
        case 0:
        case 2:
            std::snprintf (t, sizeof (t), "%d Hz", (int) std::lround (30.0 * std::pow (533.33, (double) raw / 65536.0)));
            return t;
        case 1:
        case 3: return raw != 0 ? "24 dB/oct" : "12 dB/oct";
        case 4: return msText (timeMs (raw));
        case 5:
            std::snprintf (t, sizeof (t), "%.1f%%", (double) raw / 10.0);
            return t;
        case 6:
            if (raw < 0)
                return "Off";
            std::snprintf (t, sizeof (t), "%.1f Hz", 5.0 * std::pow (16.0, (double) raw / 65536.0));
            return t;
        default: break;
    }
    if (index < 7 || index >= 59)
        return {};
    const auto band = (index - 7) / 13, j = (index - 7) % 13;
    switch (j)
    {
        case 0:
        {
            static const char* modes[4] { "On", "Compression off", "Muted", "Off" };
            return modes[std::max (0, std::min (3, raw))];
        }
        case 1:
        case 2:
        case 5: return gainText (raw);
        case 3:
            std::snprintf (t, sizeof (t), "%+.0f%%", (double) raw / 10.0);
            return t;
        case 4:
            if (raw == 0)
                return "No saturation";
            std::snprintf (t, sizeof (t), "%s %.0f%%", raw < 0 ? "Mode A" : "Mode B", std::abs ((double) raw) / 10.0);
            return t;
        case 6: return raw == 0 && band < 3 ? "Default" : msText (timeMs (raw));
        case 7: return raw == 0 ? "Default" : msText (2.0 * timeMs (raw));
        case 9: return msText (2.0 * timeMs (raw));
        case 8:
        case 10:
            std::snprintf (t, sizeof (t), "%d", raw);
            return t;
        case 11: return msText (timeMs (raw));
        case 12: return raw != 0 ? "RMS" : "Peak";
        default: return {};
    }
}

std::vector<std::pair<float, float>> curvePoints (const std::vector<std::uint8_t>& slotState, int band)
{
    std::vector<std::pair<float, float>> out;
    const auto blob = unpack (slotState);
    if (! blob || band < 0 || band > 3)
        return out;
    const auto envelopes = hlfx::maximusEnvelopes (blob->state);
    if (envelopes.size() < 8)
        return out;
    double x = 0.0;
    for (const auto& [dx, y] : envelopes[(std::size_t) (2 * band)])
    {
        x += dx;
        out.emplace_back ((float) std::min (1.0, x), std::max (0.0f, std::min (1.0f, y)));
    }
    return out;
}

std::vector<CurveEditPoint> curveForEditing (const std::vector<std::uint8_t>& slotState, int band)
{
    std::vector<CurveEditPoint> out;
    const auto blob = unpack (slotState);
    if (! blob || band < 0 || band > 3)
        return out;
    double x = 0.0;
    for (const auto& p : hlfx::maximusCurve (blob->state, 2 * band))
    {
        x += p.dx;
        out.push_back ({ x, p.y, p.tension, p.type });
    }
    return out;
}

std::vector<std::uint8_t> withCurve (const std::vector<std::uint8_t>& slotState, int band, const std::vector<CurveEditPoint>& points)
{
    auto blob = unpack (slotState);
    if (! blob || band < 0 || band > 3 || points.empty())
        return slotState;
    const auto old = hlfx::maximusCurve (blob->state, 2 * band);
    std::vector<double> oldX;
    double x = 0.0;
    for (const auto& p : old)
        oldX.push_back (x += p.dx);
    std::vector<hlfx::MaximusCurvePoint> pts;
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        hlfx::MaximusCurvePoint p;
        const auto prev = i == 0 ? 0.0 : points[i - 1].x;
        p.dx = std::max (0.0, points[i].x - prev);
        // a segment whose two ends did not move keeps its stored Δx (no rounding drift on write → read → write)
        if (i < old.size() && points[i].x == oldX[i] && (i == 0 || points[i - 1].x == oldX[i - 1]))
            p.dx = old[i].dx;
        p.y = std::max (0.0f, std::min (1.0f, points[i].y));
        p.tension = std::max (-1.0f, std::min (1.0f, points[i].tension));
        p.type = points[i].type;
        pts.push_back (p);
    }
    blob->state = hlfx::withMaximusCurve (blob->state, 2 * band, pts);
    return pack (*blob);
}

int addCurvePoint (std::vector<CurveEditPoint>& points, double x, float y, float tension, std::uint8_t type)
{
    if (points.size() < 2)
        return -1;
    x = std::max (0.0, std::min (1.0, x));
    // after the last point at the same x (FL inserts behind equal times)
    std::size_t at = 1;
    while (at < points.size() - 1 && points[at].x <= x)
        ++at;
    x = std::max (points[at - 1].x, std::min (points[at].x, x));
    points.insert (points.begin() + (std::ptrdiff_t) at, CurveEditPoint { x, std::max (0.0f, std::min (1.0f, y)), tension, type });
    return (int) at;
}

int moveCurvePoint (std::vector<CurveEditPoint>& points, int index, double x, float y)
{
    if (index < 0 || index >= (int) points.size())
        return -1;
    auto& p = points[(std::size_t) index];
    p.y = std::max (0.0f, std::min (1.0f, y));
    if (index == 0)
        p.x = 0.0;
    else if (index == (int) points.size() - 1)
        p.x = std::max (p.x, points[(std::size_t) index - 1].x); // stays at the end
    else
        p.x = std::max (points[(std::size_t) index - 1].x, std::min (points[(std::size_t) index + 1].x, x));
    return index;
}

bool removeCurvePoint (std::vector<CurveEditPoint>& points, int index)
{
    // FL: "The first point of the envelope can't be removed."; the last one holds the curve's end at x = 1
    if (index <= 0 || index >= (int) points.size() - 1)
        return false;
    points.erase (points.begin() + index);
    return true;
}

std::vector<float> curveShape (const std::vector<CurveEditPoint>& points, int n)
{
    std::vector<hlfx::MaximusCurvePoint> pts;
    double prev = 0.0;
    for (const auto& p : points)
    {
        pts.push_back ({ std::max (0.0, p.x - prev), p.y, p.tension, p.type });
        prev = p.x;
    }
    return hlfx::maximusCurveShape (pts, n);
}

std::string curveLevelText (double v)
{
    const auto level = (std::pow (3.0, 2.0 * std::max (0.0, v)) - 1.0) * 0.5;
    if (level <= 1.0e-6)
        return "-inf dB";
    char t[32];
    const auto db = 20.0 * std::log10 (level);
    std::snprintf (t, sizeof (t), "%+.1f dB", std::fabs (db) < 0.05 ? 0.0 : db);
    return t;
}

int selectedBand (const std::vector<std::uint8_t>& slotState)
{
    const auto blob = unpack (slotState);
    if (! blob || blob->state.size() < 0x1e4)
        return 3;
    std::int32_t b = 3;
    std::memcpy (&b, blob->state.data() + 0x1e0, 4);
    return b >= 0 && b <= 3 ? (int) b : 3;
}
} // namespace lynx::maximusfx
