// Kilohearts plug-in states → LYNX stand-ins (round 190ln; included by core/PluginSubstitutes.cpp, inside its anonymous
// namespace). docs/PLUGIN_FALLBACK.md "Kilohearts", docs/analise/REA_rodada190ln.md.
//
// [DATA] A Kilohearts component state (VST3; the VST2 chunk holds the same block) is u32 1, u32 the size of what follows,
// then a ZIP archive with one entry, state.json (deflated): {"meta", "version", "preset", "model"}. The model's top level
// holds the effect's settings in the plug-in's own units, every value a JSON string ("order": "16", "cutoff": "130",
// "threshold": "0.5011872" as a linear gain, "type": "low_pass"), and "$type" names the effect ("disperser", "chorus",
// "quick_eq" …). Read on the states the plug-ins save through their public parameters (refcap030/Kilohearts/<plug-in>/
// st/, round 190ln: every control at five values or every step), whose texts give each control's unit and range.
// What each control does is the plug-ins' public manuals' (Snapins / Faturator operator's manuals); the stand-ins are
// LYNX's own effects set to the same units. Nothing from the plug-ins' code or presets.

namespace khs
{
// ---- a small JSON reader (the subset the states use: objects, arrays, strings; numbers / literals kept as text) ----
struct Json
{
    enum class Type : std::uint8_t { null, string, object, array };
    Type type = Type::null;
    std::string text;
    // object: names[k] -> values[k] (rollup step 9: two vectors, not std::pair<std::string, Json> — a pair of the type
    // being defined is ill-formed, and clang's libstdc++ checks reject it; std::vector of an incomplete type is allowed)
    std::vector<std::string> names;
    std::vector<Json> values;
    std::vector<Json> items;                           // array
    const Json* get (std::string_view key) const noexcept
    {
        for (std::size_t k = 0; k < names.size() && k < values.size(); ++k)
            if (names[k] == key)
                return &values[k];
        return nullptr;
    }
};

class JsonReader
{
public:
    explicit JsonReader (std::string_view t) : s (t) {}
    bool parse (Json& out, int depth = 0)
    {
        skip();
        if (p >= s.size() || depth > 64)
            return false;
        const char c = s[p];
        if (c == '{')
        {
            out.type = Json::Type::object;
            ++p;
            skip();
            if (p < s.size() && s[p] == '}')
                return ++p, true;
            for (;;)
            {
                skip();
                std::string key;
                if (! str (key))
                    return false;
                skip();
                if (p >= s.size() || s[p] != ':')
                    return false;
                ++p;
                Json v;
                if (! parse (v, depth + 1))
                    return false;
                out.names.push_back (std::move (key));
                out.values.push_back (std::move (v));
                skip();
                if (p < s.size() && s[p] == ',')
                {
                    ++p;
                    continue;
                }
                if (p < s.size() && s[p] == '}')
                    return ++p, true;
                return false;
            }
        }
        if (c == '[')
        {
            out.type = Json::Type::array;
            ++p;
            skip();
            if (p < s.size() && s[p] == ']')
                return ++p, true;
            for (;;)
            {
                Json v;
                if (! parse (v, depth + 1))
                    return false;
                out.items.push_back (std::move (v));
                skip();
                if (p < s.size() && s[p] == ',')
                {
                    ++p;
                    continue;
                }
                if (p < s.size() && s[p] == ']')
                    return ++p, true;
                return false;
            }
        }
        out.type = Json::Type::string;
        if (c == '"')
            return str (out.text);
        // a bare number / true / false / null: its text
        const auto start = p;
        while (p < s.size() && s[p] != ',' && s[p] != '}' && s[p] != ']' && ! std::isspace ((unsigned char) s[p]))
            ++p;
        out.text = std::string (s.substr (start, p - start));
        return p > start;
    }

private:
    std::string_view s;
    std::size_t p = 0;
    void skip() noexcept
    {
        while (p < s.size() && std::isspace ((unsigned char) s[p]))
            ++p;
    }
    bool str (std::string& out)
    {
        if (p >= s.size() || s[p] != '"')
            return false;
        ++p;
        while (p < s.size() && s[p] != '"')
        {
            if (s[p] == '\\' && p + 1 < s.size())
            {
                const char e = s[p + 1];
                out += e == 'n' ? '\n' : e == 't' ? '\t' : e;
                p += 2;
                if (e == 'u') // \uXXXX: kept as '?' (names only)
                    p = std::min (s.size(), p + 4), out.back() = '?';
                continue;
            }
            out += s[p++];
        }
        if (p >= s.size())
            return false;
        ++p;
        return true;
    }
};

// The state.json of a Kilohearts state (the ZIP may sit inside a host's wrapper: found by its local-header signature)
std::optional<Json> readState (const std::vector<std::uint8_t>& state)
{
    const auto pk = findBytes (state, std::string_view ("PK\x03\x04", 4));
    if (pk == std::string::npos)
        return std::nullopt;
    std::size_t size = state.size() - pk;
    if (pk >= 8)
    {
        std::uint32_t version = 0, declared = 0;
        std::memcpy (&version, state.data() + pk - 8, 4);
        std::memcpy (&declared, state.data() + pk - 4, 4);
        if (version == 1 && declared >= 22 && declared <= size)
            size = declared;
    }
    zip::Reader reader;
    if (! reader.open (state.data() + pk, size))
        return std::nullopt;
    const auto json = reader.extract ("state.json", nullptr, 16u << 20);
    if (! json)
        return std::nullopt;
    Json root;
    JsonReader r (std::string_view ((const char*) json->data(), json->size()));
    if (! r.parse (root) || root.type != Json::Type::object)
        return std::nullopt;
    const auto* model = root.get ("model");
    if (model == nullptr || model->type != Json::Type::object)
        return std::nullopt;
    return *model;
}

// ---- the model's values -------------------------------------------------------------------------------------------
struct Model
{
    const Json& m;
    std::string type() const { return str ("$type"); }
    std::string str (std::string_view key) const
    {
        const auto* v = m.get (key);
        return v != nullptr && v->type == Json::Type::string ? v->text : std::string();
    }
    bool has (std::string_view key) const { return m.get (key) != nullptr; }
    double num (std::string_view key, double fallback) const
    {
        float f = 0;
        const auto t = str (key);
        return ! t.empty() && parseFloat (t, f) && std::isfinite (f) ? (double) f : fallback;
    }
    bool flag (std::string_view key, bool fallback) const
    {
        const auto t = str (key);
        return t == "true" ? true : t == "false" ? false : fallback;
    }
};

// round 190nb: the measured revision of the maps (1). A stand-in is built from the original's state when a project
// opens without the plug-in; a project saved with the stand-in keeps the stand-in's own values, so nothing saved changes.
// 0 = round 190ln's maps (kept for A / B against the measured references).
constexpr int translatorRevision = 1;
constexpr bool revision190nb = translatorRevision >= 1;
constexpr double kReverbWet = 1.0, kReverbEarly = 1.0; // kHs Reverb's tank / early level on Reeverb 2's (round 190nb)
constexpr int volumeGateSteps = 32; // Volume gate: 32 steps per 4-beat bar (core/Effects.cpp)
double linToDb (double g) noexcept { return 20.0 * std::log10 (std::max (g, 1e-9)); }

// ---- the stand-in's value for a unit: inverse of the effect's own parameter text (formatEffectParam) ----------------
// The number in a parameter text, in base units: Hz (kHz × 1000), ms (s × 1000), dB, %, semitones, plain.
std::optional<double> textNumber (const std::string& t)
{
    std::size_t i = 0;
    while (i < t.size() && ! (std::isdigit ((unsigned char) t[i]) || ((t[i] == '-' || t[i] == '+' || t[i] == '.') && i + 1 < t.size() && (std::isdigit ((unsigned char) t[i + 1]) || t[i + 1] == '.'))))
        ++i;
    if (i >= t.size())
        return std::nullopt;
    char* end = nullptr;
    const double v = std::strtod (t.c_str() + i, &end);
    if (end == t.c_str() + i)
        return std::nullopt;
    const std::string unit (end);
    if (unit.find ("kHz") != std::string::npos)
        return v * 1000.0;
    // seconds ("1.2 s", "2.0s"), not "ms" / "st"
    for (std::size_t k = 0; k < unit.size(); ++k)
        if (unit[k] == 's' && (k == 0 || unit[k - 1] == ' ') && (k + 1 >= unit.size() || ! std::isalpha ((unsigned char) unit[k + 1])))
            return v * 1000.0;
    return v;
}

// The normalised value whose text reads `target` (base units as textNumber), searched in [lo, hi]: 64 samples find the
// span whose texts bracket the target (texts that are not numbers — "off", "-inf dB" — are skipped), bisection inside it,
// then the middle of the run of values that show the same rounded text. No bracket: the closest sample.
float valueFor (const EffectSlot& slot, int param, double target, float lo = 0.0f, float hi = 1.0f)
{
    auto at = [&] (float v) -> std::optional<double> {
        const auto y = textNumber (formatEffectParam (slot.kind, param, v, 120.0, slot.params));
        return y && std::isfinite (*y) ? y : std::nullopt;
    };
    constexpr int samples = 64;
    const double tol = 1e-6 * std::max (1.0, std::abs (target)); // the plug-ins' floats (0.01 s = 9.9999998 ms)
    std::optional<double> prev;
    float prevX = lo, best = lo;
    double bestErr = std::numeric_limits<double>::infinity();
    for (int i = 0; i <= samples; ++i)
    {
        const float x = lo + (hi - lo) * (float) i / samples;
        const auto y = at (x);
        if (y && std::abs (*y - target) <= tol && (i == 0 || i == samples))
            return x; // the target at an end of the range: that end (not the middle of its rounded run)
        if (y && std::abs (*y - target) < bestErr)
            bestErr = std::abs (*y - target), best = x;
        if (y && prev && *y != *prev && (target - *prev) * (target - *y) <= 0.0)
        {
            const bool up = *y > *prev;
            // first value whose text reaches the target (>= going up), then the first that passes it (that one may lie
            // past this span: the run of equal texts goes on until a sample shows another)
            float spanEnd = x;
            for (int j = i + 1; j <= samples; ++j)
            {
                const float xj = lo + (hi - lo) * (float) j / samples;
                const auto yj = at (xj);
                spanEnd = xj;
                if (! yj || (up ? *yj > target + tol : *yj < target - tol))
                    break;
            }
            auto edge = [&] (bool strict) {
                // strict: the last value whose text does not pass the target; else the first that reaches it
                float x0 = prevX, x1 = strict ? spanEnd : x;
                for (int k = 0; k < 40; ++k)
                {
                    const float mid = 0.5f * (x0 + x1);
                    const auto ym = at (mid);
                    if (! ym)
                        break;
                    const bool before = up ? (strict ? *ym <= target + tol : *ym < target - tol) : (strict ? *ym >= target - tol : *ym > target + tol);
                    (before ? x0 : x1) = mid;
                }
                return strict ? x0 : x1;
            };
            const float a = edge (false), b = edge (true);
            return b > a ? 0.5f * (a + b) : a;
        }
        if (y)
            prev = y, prevX = x;
    }
    return best;
}

void setUnit (EffectSlot& slot, int param, double target, int& count, float lo = 0.0f, float hi = 1.0f)
{
    if (param < 0 || param >= maxEffectParams || ! std::isfinite (target))
        return;
    slot.params[(std::size_t) param] = std::clamp (valueFor (slot, param, target, lo, hi), 0.0f, 1.0f);
    ++count;
}
void setRaw (EffectSlot& slot, int param, double v, int& count)
{
    if (param < 0 || param >= maxEffectParams || ! std::isfinite (v))
        return;
    slot.params[(std::size_t) param] = (float) std::clamp (v, 0.0, 1.0);
    ++count;
}
// a named parameter of the stand-in (its position in effectInfo), −1 when absent
int paramNamed (EffectKind kind, std::string_view name)
{
    const auto& info = effectInfo (kind);
    for (std::size_t i = 0; i < info.params.size(); ++i)
        if (name == info.params[i].name)
            return (int) i;
    return -1;
}
} // namespace khs

// ---- the translator ------------------------------------------------------------------------------------------------
// One translator for every Kilohearts entry: the model's "$type" picks the mapping, which acts only when the slot is the
// stand-in that entry names (a state of another plug-in, or a damaged one, leaves the entry's defaults).
int translateKilohearts (const std::vector<std::uint8_t>& state, EffectSlot& slot)
{
    using namespace khs;
    const auto root = readState (state);
    if (! root)
        return 0;
    const Model m { *root };
    const auto type = m.type();
    const auto kind = slot.kind;
    int n = 0;
    auto P = [&] (std::string_view name) { return paramNamed (kind, name); };
    auto unit = [&] (std::string_view name, double v, float lo = 0.0f, float hi = 1.0f) { setUnit (slot, P (name), v, n, lo, hi); };
    auto raw = [&] (std::string_view name, double v) { setRaw (slot, P (name), v, n); };
    const double mix = std::clamp (m.num ("mix", 1.0), 0.0, 1.0);

    if (type == "gain" && kind == EffectKind::utility)
    {
        // the stored gain is linear in both display modes (dB / %); the plug-in's range is ±30 dB
        unit ("Gain", linToDb (m.num ("gain", 1.0)), 1e-4f, 1.0f);
        return n;
    }
    if (type == "matrix" && kind == EffectKind::utility)
    {
        unit ("L -> L", m.num ("l2l", 1.0));
        unit ("R -> L", m.num ("r2l", 0.0));
        unit ("L -> R", m.num ("l2r", 0.0));
        unit ("R -> R", m.num ("r2r", 1.0));
        return n;
    }
    if (type == "stereo" && kind == EffectKind::utility)
    {
        // mid / side gains (width = the side's, mid = the mid's) as the channel matrix, then the pan as the balance
        const double w = m.num ("width", 1.0), md = m.num ("mid", 1.0);
        unit ("L -> L", 0.5 * (md + w));
        unit ("R -> R", 0.5 * (md + w));
        unit ("R -> L", 0.5 * (md - w));
        unit ("L -> R", 0.5 * (md - w));
        raw ("Balance", 0.5 + 0.5 * std::clamp (m.num ("pan", 0.0), -1.0, 1.0));
        return n;
    }
    if (type == "haas" && kind == EffectKind::stereoShaper)
    {
        // "channel" true = the right channel is delayed (the plug-in's default, its texts "Left" / "Right"); Stereo
        // Shaper's Delay reads "L … ms" below its centre (left delayed) and "R … ms" above
        const double ms = 1000.0 * std::clamp (m.num ("delay", 0.005), 0.0001, 0.1);
        if (m.flag ("channel", true))
            unit ("Delay", ms, 0.5001f, 1.0f);
        else
            unit ("Delay", ms, 0.0f, 0.4999f);
        return n;
    }
    if (type == "filter" && kind == EffectKind::facetEq)
    {
        namespace f = facet;
        const auto t = m.str ("type");
        const int slope = std::clamp ((int) std::lround (m.num ("slope", 1.0)), 1, 6);
        const double q = std::clamp (m.num ("q", 0.707), 0.025, 40.0);
        f::Shape shape = f::Shape::bell;
        if (t == "low_pass") shape = f::Shape::highCut;
        else if (t == "high_pass") shape = f::Shape::lowCut;
        else if (t == "band_pass") shape = f::Shape::bandPass;
        else if (t == "notch") shape = f::Shape::notch;
        else if (t == "low_shelf") shape = f::Shape::lowShelf;
        else if (t == "high_shelf") shape = f::Shape::highShelf;
        else if (t != "peak") return 0;
        slot.params[(std::size_t) f::param (0, 0)] = f::shapeValue (shape);
        slot.params[(std::size_t) f::param (0, 1)] = f::hzValue (m.num ("cutoff", 620.0));
        slot.params[(std::size_t) f::param (0, 2)] = f::gainValue (m.num ("gain", 0.0));
        // cuts: the slope multiplies a 12 dB/oct section (Facet's Q 1 = Butterworth: Q / 0.707); band pass / notch /
        // bell: Facet's bandwidth Q is the cookbook Q × √2 for those shapes (qb = Q / √2)
        const bool cut = shape == f::Shape::lowCut || shape == f::Shape::highCut;
        const bool bw = shape == f::Shape::bandPass || shape == f::Shape::notch;
        slot.params[(std::size_t) f::param (0, 3)] = f::qValue (cut ? q / std::sqrt (0.5) : bw ? q * std::sqrt (2.0) : q);
        slot.params[(std::size_t) f::param (0, 4)] = f::slopeValue (cut ? std::min (96, 12 * slope) : 12);
        if (cut && slope > 1)
            slot.params[(std::size_t) f::cutLawParam] = 1.0f; // every section carries the Q (cascaded identical filters)
        return 5;
    }
    if (type == "disperser" && kind == EffectKind::facetEq)
    {
        // [STATIC] the plug-in runs `order` cookbook all-pass sections in a row at the cutoff with Q = pinch (its DSP
        // object builds each section from the all-pass case of its biquad designer); Facet's hidden all-pass cascade
        // is that, its bands left off
        namespace f = facet;
        const int order = std::clamp ((int) std::lround (m.num ("order", 16.0)), 0, f::maxDispersionSections);
        slot.params[(std::size_t) f::dispersionCountParam] = (float) order / (float) f::maxDispersionSections;
        slot.params[(std::size_t) f::dispersionFreqParam] = f::hzValue (std::clamp (m.num ("cutoff", 130.0), 20.0, 20000.0));
        slot.params[(std::size_t) f::dispersionQParam] = f::qValue (std::clamp (m.num ("q", 0.5), 0.5, 16.0));
        return 3;
    }
    if (type == "quick_eq" && kind == EffectKind::facetEq)
    {
        // three bands split at two frequencies: the mid gain as the output, the low / high bands as shelves relative to it
        namespace f = facet;
        const double lo = m.num ("low_gain", 0.0), md = m.num ("mid_gain", 0.0), hi = m.num ("high_gain", 0.0);
        slot.params[(std::size_t) f::param (0, 0)] = f::shapeValue (f::Shape::lowShelf);
        slot.params[(std::size_t) f::param (0, 1)] = f::hzValue (m.num ("low_split", 220.0));
        slot.params[(std::size_t) f::param (0, 2)] = f::gainValue (lo - md);
        slot.params[(std::size_t) f::param (0, 3)] = f::qValue (0.5);
        slot.params[(std::size_t) f::param (0, 4)] = f::slopeValue (12);
        slot.params[(std::size_t) f::param (1, 0)] = f::shapeValue (f::Shape::highShelf);
        slot.params[(std::size_t) f::param (1, 1)] = f::hzValue (m.num ("high_split", 2200.0));
        slot.params[(std::size_t) f::param (1, 2)] = f::gainValue (hi - md);
        slot.params[(std::size_t) f::param (1, 3)] = f::qValue (0.5);
        slot.params[(std::size_t) f::param (1, 4)] = f::slopeValue (12);
        slot.params[(std::size_t) f::outputParam] = (float) std::clamp ((md + 24.0) / 48.0, 0.0, 1.0);
        return 6;
    }
    if (type == "vocal_filter" && kind == EffectKind::facetEq && revision190nb)
    {
        // round 190nb [STATIC, HeartCore 2.4: VocalFilterDsp's process + the shared cookbook biquad designer]: two biquads
        // in a row — at the lower cutoff a peak (Lows on) or a high pass (off), at the higher one a peak (Highs on) or
        // a low pass (off), every one at the plug-in's Q; the peaks' gain is 20·log10 (Q) dB (Q = 4: +12 dB)
        namespace f = facet;
        const double q = std::clamp (m.num ("q", 4.0), 0.1, 40.0);
        const double c1 = m.num ("cutoff_1", 550.0), c2 = m.num ("cutoff_2", 1500.0);
        const double lo = std::min (c1, c2), hi = std::max (c1, c2);
        const double peakDb = 20.0 * std::log10 (q);
        auto band = [&] (int b, double hz, bool peak, f::Shape cut) {
            slot.params[(std::size_t) f::param (b, 0)] = f::shapeValue (peak ? f::Shape::bell : cut);
            slot.params[(std::size_t) f::param (b, 1)] = f::hzValue (hz);
            slot.params[(std::size_t) f::param (b, 2)] = f::gainValue (peak ? peakDb : 0.0);
            slot.params[(std::size_t) f::param (b, 3)] = f::qValue (peak ? q : q / std::sqrt (0.5)); // (Facet's Q 1 = Butterworth for cuts)
            slot.params[(std::size_t) f::param (b, 4)] = f::slopeValue (12);
        };
        band (0, lo, m.flag ("lows", true), f::Shape::lowCut);
        band (1, hi, m.flag ("highs", true), f::Shape::highCut);
        return 5;
    }
    if (type == "vocal_filter" && kind == EffectKind::facetEq)
    {
        // two formant peaks (bells of the plug-in's Q, +12 dB) and, with Lows / Highs off, cuts outside them
        namespace f = facet;
        const double q = std::clamp (m.num ("q", 4.0), 0.1, 40.0);
        int b = 0;
        for (const char* key : { "cutoff_1", "cutoff_2" })
        {
            slot.params[(std::size_t) f::param (b, 0)] = f::shapeValue (f::Shape::bell);
            slot.params[(std::size_t) f::param (b, 1)] = f::hzValue (m.num (key, b == 0 ? 550.0 : 1500.0));
            slot.params[(std::size_t) f::param (b, 2)] = f::gainValue (12.0);
            slot.params[(std::size_t) f::param (b, 3)] = f::qValue (q);
            ++b;
        }
        if (! m.flag ("lows", true))
        {
            slot.params[(std::size_t) f::param (2, 0)] = f::shapeValue (f::Shape::lowCut);
            slot.params[(std::size_t) f::param (2, 1)] = f::hzValue (m.num ("cutoff_1", 550.0) * 0.7);
            slot.params[(std::size_t) f::param (2, 4)] = f::slopeValue (12);
        }
        if (! m.flag ("highs", true))
        {
            slot.params[(std::size_t) f::param (3, 0)] = f::shapeValue (f::Shape::highCut);
            slot.params[(std::size_t) f::param (3, 1)] = f::hzValue (m.num ("cutoff_2", 1500.0) * 1.4);
            slot.params[(std::size_t) f::param (3, 4)] = f::slopeValue (12);
        }
        slot.params[(std::size_t) f::outputParam] = (float) ((-6.0 + 24.0) / 48.0);
        return 5;
    }
    if ((type == "ladder" || type == "dirty_filter") && kind == EffectKind::filter)
    {
        // the ladder: a 24 dB/oct low pass, resonance 0 … 1 → Q 0.5 … 12 (log); the nonlinear filter: its type, Q and cutoff
        const auto t = m.str ("filter_type");
        raw ("Type", t == "high_pass" ? 0.5 : t == "band_pass" ? 0.9 : 0.1);
        unit ("Cutoff", m.num ("cutoff", 440.0));
        if (type == "ladder")
        {
            const double r = std::clamp (m.num ("resonance", 0.0), 0.0, 1.0);
            unit ("Reso.", 0.5 * std::pow (24.0, r));
            raw ("Slope", 1.0);
        }
        else
            unit ("Reso.", m.num ("q", 0.707));
        return n;
    }
    if (type == "compressor" && kind == EffectKind::compressor)
    {
        const double thr = linToDb (m.num ("threshold", 0.5011872)), ratio = std::max (1.0, m.num ("ratio", 2.0));
        unit ("Threshold", thr);
        unit ("Ratio", std::min (20.0, ratio));
        unit ("Attack", 1000.0 * m.num ("attack", 0.023));
        unit ("Release", 1000.0 * m.num ("release", 0.023));
        // Makeup (0 … 100 %): the share of the reduction a 0 dBFS peak gets, given back
        unit ("Makeup", m.num ("makeup", 0.0) * std::max (0.0, -thr) * (1.0 - 1.0 / ratio));
        return n;
    }
    if ((type == "compactor" || type == "dynamics") && kind == EffectKind::compressor)
    {
        if (type == "compactor")
        {
            unit ("Threshold", linToDb (m.num ("threshold", 1.0)));
            unit ("Ratio", 20.0);
            unit ("Attack", 1000.0 * m.num ("env_attack", 0.005));
            unit ("Release", 1000.0 * m.num ("env_release", 0.005));
        }
        else
        {
            // the downward part above the high threshold (its ratio is stored as the slope, 1 / ratio; the upward /
            // expander part below the low threshold is not modelled); attack / release are multiples of the plug-in's
            // own times (100 % = 1): taken as 10 ms / 100 ms
            const double slopeHi = m.num ("high_ratio", 1.0);
            unit ("Threshold", m.num ("high_threshold", -20.0) - m.num ("in_gain", 0.0));
            unit ("Ratio", std::clamp (slopeHi > 0.0 ? 1.0 / slopeHi : 20.0, 1.0, 20.0));
            unit ("Attack", 10.0 * m.num ("attack", 1.0));
            unit ("Release", 100.0 * m.num ("release", 1.0));
            unit ("Makeup", std::max (0.0, m.num ("in_gain", 0.0) + m.num ("out_gain", 0.0)));
        }
        raw ("Mix", mix);
        return n;
    }
    if (type == "limiter" && kind == EffectKind::limiter)
    {
        // post · limit_thr (pre · x) = limit_(thr · post) (pre · post · x): the plug-in's three gains on two controls
        const double pre = linToDb (m.num ("pre_gain", 1.0)), post = linToDb (m.num ("post_gain", 1.0));
        unit ("Ceiling", linToDb (m.num ("threshold", 1.0)) + post);
        unit ("Gain", pre + post);
        unit ("Release", 1000.0 * m.num ("release", 0.023));
        return n;
    }
    if (type == "clipper" && kind == EffectKind::fruitySoftClipper)
    {
        unit ("Threshold", linToDb (m.num ("threshold", 1.0) * m.num ("pre_gain", 1.0)));
        unit ("Post", linToDb (m.num ("post_gain", 1.0)));
        return n;
    }
    if (type == "gate" && kind == EffectKind::sentryGate)
    {
        unit ("Threshold", linToDb (m.num ("threshold", 0.0316)));
        unit ("Attack", 1000.0 * m.num ("attack", 0.005));
        unit ("Hold", 1000.0 * m.num ("hold", 0.025));
        unit ("Release", 1000.0 * m.num ("release", 0.025));
        // range: the closed gate's attenuation as a share of full (1 = silent)
        const double r = std::clamp (m.num ("range", 1.0), 0.0, 1.0);
        unit ("Range", r >= 0.999 ? 100.0 : -linToDb (1.0 - r));
        raw ("Lookahead on", m.flag ("look_ahead", true) ? 1.0 : 0.0);
        if (m.flag ("look_ahead", true))
            unit ("Lookahead", 5.0);
        return n;
    }
    if (type == "transient_shaper" && kind == EffectKind::strikeShaper)
    {
        // attack / sustain −100 … +100 % → Strike Shaper's attack / sustain gains (±12 dB taken for the ends: not
        // measured, the plug-in renders only its defaults here); its output clip → Strike's guard
        unit ("Attack", 12.0 * std::clamp (m.num ("attack", 0.0), -1.0, 1.0));
        if (revision190nb)
        {
            // round 190nb [MEASURED on kHs Transient Shaper]: Sustain is a level-independent gain on the steady part,
            // 17.93 dB × sustain (the stairs probe: −17.93 / −8.97 / +8.97 / +17.93 dB at −100 / −50 / +50 / +100 % on
            // every step from −62 to −12 dBFS); Strike Shaper's Sustain shapes decays instead, so the gain goes on its
            // Output (−20 … +6 dB: a boost above +6 dB is cut there)
            unit ("Sustain", 0.0);
            if (const double sus = std::clamp (m.num ("sustain", 0.0), -1.0, 1.0); sus != 0.0)
                unit ("Output", std::clamp (17.93 * sus, -20.0, 6.0)); // (at 0 the default, exactly 0 dB)
        }
        else
            unit ("Sustain", 12.0 * std::clamp (m.num ("sustain", 0.0), -1.0, 1.0));
        raw ("Guard", m.flag ("clip", false) ? 1.0 : 0.0);
        return n;
    }
    if (type == "distortion" && kind == EffectKind::distortion)
    {
        unit ("Drive", std::max (0.0, linToDb (m.num ("drive", 2.0))));
        raw ("Mix", mix);
        return n;
    }
    if (type == "faturator" && kind == EffectKind::distortion)
    {
        // 0 … 100 % → 0 … 24 dB into the shaper, half of it taken back at the output (its manual: it "preserves the
        // dynamics"; neither law measured — the unlicensed plug-in renders only its input here)
        const double drive = 24.0 * std::clamp (m.num ("drive", 0.52), 0.0, 1.0);
        unit ("Drive", drive);
        unit ("Output", -0.5 * drive);
        raw ("Mix", mix);
        return n;
    }
    if (type == "bitcrush" && kind == EffectKind::lofi)
    {
        unit ("Rate", m.num ("sample_rate", 6000.0));
        unit ("Bits", m.flag ("quantize", true) ? m.num ("bit_depth", 16.0) : 24.0);
        raw ("Mix", std::clamp (m.num ("mix2", mix), 0.0, 1.0));
        return n;
    }
    if (type == "frequency_shifter" && kind == EffectKind::freqShift)
    {
        unit ("Shift", m.num ("shift", 0.0));
        raw ("Mix", 1.0);
        return n;
    }
    if (type == "pitch_shift" && kind == EffectKind::pitchShift)
    {
        const double p = m.num ("pitch", 0.0);
        const double st = std::round (p);
        unit ("Semitones", st);
        unit ("Cents", 100.0 * (p - st));
        unit ("Window", 1000.0 * m.num ("grain_size", 0.08));
        raw ("Mix", mix);
        return n;
    }
    if (type == "chorus" && kind == EffectKind::chorus)
    {
        unit ("Rate", m.num ("rate", 0.6));
        unit ("Depth", 1000.0 * m.num ("depth", 0.004));
        raw ("Mix", 0.5 * mix); // the plug-in's 100 % is its voices against the dry: an equal blend
        return n;
    }
    if (type == "unison" && kind == EffectKind::chorus)
    {
        unit ("Rate", 0.5 + 2.0 * std::clamp (m.num ("detune", 0.25), 0.0, 1.0));
        unit ("Depth", 1.0 + 6.0 * std::clamp (m.num ("detune", 0.25), 0.0, 1.0));
        raw ("Mix", 0.5 * mix);
        return n;
    }
    if (type == "phaser" && kind == EffectKind::fruityPhaser)
    {
        // order k → 2k all-pass stages; the cutoff ± depth (in octaves, ±2 at 100 %) as the sweep's range, the rate as
        // the sweep; the plug-in's 100 % mix is the phaser's own blend of dry and shifted (Fruity's 50 % wet)
        unit ("Stages", 2.0 * std::clamp (m.num ("order", 3.0), 1.0, 11.0));
        unit ("Sweep freq.", m.num ("rate", 0.6));
        const double d = std::clamp (m.num ("depth", 0.5), 0.0, 1.0);
        unit ("Min depth", 100.0 * std::clamp (0.5 - 0.4 * d, 0.0, 1.0));
        unit ("Max depth", 100.0 * std::clamp (0.5 + 0.4 * d, 0.0, 1.0));
        unit ("Feedback", 0.0);
        unit ("Dry-wet", 50.0 * mix);
        unit ("Out gain", 0.0);
        unit ("Stereo", 180.0 * std::clamp (m.num ("spread", 0.5), 0.0, 1.0));
        return n;
    }
    if (type == "flanger" && kind == EffectKind::fruityFlanger)
    {
        unit ("Delay", 1000.0 * m.num ("delay", 0.001));
        unit ("Depth", 1000.0 * m.num ("mod_depth", 0.001));
        unit ("Rate", m.num ("mod_rate", 0.3));
        const double fb = std::clamp (m.num ("feedback", 0.0), -0.98, 0.98);
        unit ("Feed", 100.0 * std::abs (fb));
        raw ("Invert feed", fb < 0.0 ? 1.0 : 0.0);
        raw ("Invert wet", 0.0);
        if (revision190nb)
        {
            // round 190nb [STATIC, FlangerDsp's process]: dry cos and wet sin of mix × 45° (100 %: an equal blend)
            unit ("Dry", 100.0 * std::cos (0.25 * khsdsp::pi * mix));
            unit ("Wet", 100.0 * std::sin (0.25 * khsdsp::pi * mix));
            raw ("Cross", 0.0); // (Fruity Flanger's default 50 % adds the other channel's wet; the plug-in has none)
        }
        else
        {
            unit ("Dry", 100.0);
            unit ("Wet", 100.0 * mix);
        }
        unit ("Phase", 180.0 * std::clamp (m.num ("spread", 0.25), 0.0, 1.0));
        return n;
    }
    if (type == "reverb" && kind == EffectKind::reeverb2)
    {
        // decay (s), size 50 … 200 % → room size 25 … 100, damping (dB/s of extra high decay) → the high damping
        // frequency (4 kHz at the plug-in's 24 dB/s), width 0 … 200 % → stereo separation, early, dry / wet
        unit ("Decay", 1000.0 * std::clamp (m.num ("decay", 3.0), 0.1, 30.0));
        unit ("Room size", 50.0 * std::clamp (m.num ("size", 1.0), 0.5, 2.0));
        unit ("High damping", 16000.0 / (1.0 + std::max (0.0, m.num ("damping", 24.0)) / 8.0));
        unit ("Stereo sep.", 100.0 * (std::clamp (m.num ("width", 1.0), 0.0, 2.0) - 1.0));
        if (! revision190nb)
        {
            unit ("Early ref.", 100.0 * std::clamp (m.num ("early", 0.25), 0.0, 1.0));
            unit ("Dry", 100.0 * (1.0 - mix));
            unit ("Wet", 100.0 * mix);
            return n;
        }
        // round 190nb [MEASURED]: the plug-in's mix is equal-power (an impulse at 25 % keeps cos 22.5° = 0.924 of its
        // level); Reeverb 2's Dry / Wet / Early are FL's level law (gain = (e^(2.398 x) − 1) / 10, x = raw / 128 for the
        // dry, raw / 256 for wet and early; normalised = raw / 160, raw / 320), so the gains are set through that law;
        // the tank's level against the plug-in's (kReverbWet) measured on the impulse renders
        auto levelRaw = [] (double gain, double den) { return std::round (den * std::log (10.0 * std::max (0.0, gain) + 1.0) / 2.397895273); };
        const double a = 0.5 * khsdsp::pi * mix;
        raw ("Dry", levelRaw (std::cos (a), 128.0) / 160.0);
        raw ("Wet", std::min (320.0, levelRaw (kReverbWet * std::sin (a), 256.0)) / 320.0);
        raw ("Early ref.", std::min (320.0, levelRaw (kReverbEarly * std::sin (a) * std::clamp (m.num ("early", 0.25), 0.0, 1.0), 256.0)) / 320.0);
        return n;
    }
    if ((type == "delay" || type == "dualdelay") && kind == EffectKind::echoform)
    {
        // a clean digital echo (Echoform's Clarity character): time in ms, or the synced note when Echoform has it
        // (note / dotted / triplet of 1/64 … 8/1), else that duration at 120 BPM; feedback, ping-pong / dual, ducking, mix
        raw ("Mode", 0.541667);
        raw ("Age", 0.0);
        raw ("Mod depth", 0.0);
        raw ("Wow", 0.0);
        raw ("Flutter", 0.0);
        const bool dual = type == "dualdelay";
        raw ("Style", dual ? 0.7 : (m.flag ("ping_pong", false) ? 0.9 : 0.1));
        double sec = m.num ("delay_time", 0.2);
        bool synced = false;
        if (m.str ("sync_mode") == "sync")
        {
            const auto den = m.str ("delay_denominator"); // denominator_16, denominator_8t …
            const bool trip = ! den.empty() && den.back() == 't';
            float d = 16;
            parseFloat (den.substr (std::min (den.size(), (std::size_t) 12), trip ? den.size() - 13 : std::string::npos), d);
            const double beats = std::max (1.0, m.num ("delay_numerator", 3.0)) * 4.0 / std::max (1.0f, d) * (trip ? 2.0 / 3.0 : 1.0);
            sec = beats * 0.5; // at 120 BPM, when no note fits
            // Echoform's notes: 1/64 … 8/1 at Note 0 … 0.9 (whole note = 4 beats), plain / dotted (×1.5) / triplet (×2/3)
            for (int k = 0; k <= 9 && ! synced; ++k)
            {
                const double note = 4.0 * std::pow (2.0, k - 6);
                const std::pair<double, double> kinds[] { { 1.0, 0.5 }, { 1.5, 0.75 }, { 2.0 / 3.0, 1.0 } };
                for (const auto& [mul, syncValue] : kinds)
                    if (std::abs (note * mul - beats) < 1e-6)
                    {
                        raw ("Sync L", syncValue);
                        raw ("Sync R", syncValue);
                        raw ("Note L", 0.1 * k);
                        raw ("Note R", 0.1 * k);
                        synced = true;
                        break;
                    }
            }
        }
        if (! synced)
        {
            raw ("Sync L", 0.0);
            raw ("Sync R", 0.0);
            if (revision190nb)
            {
                // round 190nb [MEASURED, kHs Delay / Dual Delay at 48 kHz]: every echo comes one sample after its time
                // (9 601 samples at 200 ms); set exactly through Echoform's own time law
                raw ("Time L", echosuite::echoform::timeValue (1000.0 * sec + 1000.0 / 48000.0));
                raw ("Time R", echosuite::echoform::timeValue (1000.0 * sec * (dual ? std::clamp (m.num ("ratio", 1.618), 0.2, 2.0) : 1.0) + 1000.0 / 48000.0));
            }
            else
            {
                unit ("Time L", 1000.0 * sec);
                unit ("Time R", 1000.0 * sec * (dual ? std::clamp (m.num ("ratio", 1.618), 0.2, 2.0) : 1.0));
            }
        }
        unit ("Feedback", 100.0 * std::clamp (m.num ("feedback", 0.5), 0.0, 1.0));
        unit ("Ducking", 100.0 * std::clamp (m.num ("duck", 0.0), 0.0, 1.0));
        if (revision190nb && ! dual)
        {
            // round 190nb [MEASURED]: the plug-in's mix is linear (dry 1 − m, wet m: an impulse at 25 / 50 / 75 % gives
            // 0.75 / 0.5 / 0.25 dry and 0.25 / 0.5 / 0.75 on the first echo); Echoform's is equal-power (dry cos, wet sin
            // of Mix × 90°) and its Output scales the echoes only: Mix gives the dry 1 − m, Output brings the echoes to m.
            // (Dual Delay's mix is linear too, but its two spread taps and cross-talk are not Echoform's dual: the
            // linear mix scored worse there, so it keeps round 190ln's)
            const double mm = std::acos (std::clamp (1.0 - mix, 0.0, 1.0)) / (0.5 * khsdsp::pi);
            raw ("Mix", mm);
            unit ("Output", 20.0 * std::log10 (std::max (1e-6, mix / std::max (1e-6, std::sin (0.5 * khsdsp::pi * mm)))));
        }
        else
            raw ("Mix", mix);
        return n;
    }
    if (type == "tape_stop" && kind == EffectKind::pocketTapeStop)
    {
        unit ("Stop time", 1000.0 * m.num ("stop_time", 0.2));
        unit ("Start time", 1000.0 * m.num ("start_time", 0.2));
        raw ("Play", m.flag ("play", true) ? 1.0 : 0.0);
        raw ("Sync", 0.0);
        return n;
    }
    if (type == "comb_filter" && kind == EffectKind::pocketComb)
    {
        raw ("Mode", 0.0); // manual pitch
        raw ("Snap", 0.0);
        unit ("Pitch", m.num ("cutoff", 440.0));
        raw ("Feedback", 0.0);
        raw ("Polarity", m.flag ("polarity", false) ? 1.0 : 0.0);
        raw ("Mix", 0.5 * mix);
        return n;
    }
    if (type == "trance_gate" && kind == EffectKind::volumeGate)
    {
        // the selected pattern's steps (on / tie) over Volume gate's 32 steps of a bar; each step's level is the mean of
        // the plug-in's envelope (linear attack, decay to the sustain, held) over the run of tied steps it opens
        const auto* pats = m.m.get ("patterns");
        const int sel = std::max (0, (int) std::lround (m.num ("pattern", 1.0)) - 1);
        if (pats == nullptr || pats->type != Json::Type::array || pats->items.empty())
            return 0;
        const auto& pat = pats->items[(std::size_t) std::min<int> (sel, (int) pats->items.size() - 1)];
        const Model pm { pat };
        const int len = std::clamp ((int) std::lround (pm.num ("length", 16.0)), 1, 64);
        const auto* steps = pat.get ("steps");
        if (steps == nullptr || steps->type != Json::Type::array)
            return 0;
        const auto rate = m.str ("rate");
        const double perBar = rate == "one_4th" ? 4 : rate == "one_8th" ? 8 : rate == "one_16th" ? 16 : rate == "one_64th" ? 64 : 32;
        const double stepSec = 2.0 / perBar; // at 120 BPM (one bar = 2 s); the envelope's shape over a step
        const double a = m.num ("attack", 0.02), d = m.num ("decay", 0.1), s = std::clamp (m.num ("sustain", 0.5), 0.0, 1.0);
        const double depth = std::clamp (m.num ("depth", 1.0), 0.0, 1.0);
        auto envMean = [&] (double T) {
            double acc = 0.0;
            const int k = 64;
            for (int i = 0; i < k; ++i)
            {
                const double t = (i + 0.5) / k * T;
                const double e = t < a ? t / std::max (a, 1e-6) : t < a + d ? 1.0 - (1.0 - s) * (t - a) / std::max (d, 1e-6) : s;
                acc += e;
            }
            return acc / k;
        };
        std::vector<double> level ((std::size_t) len, 0.0);
        for (int i = 0; i < len; ++i)
        {
            const Model st { i < (int) steps->items.size() ? steps->items[(std::size_t) i] : pat };
            const bool on = st.flag ("on", false);
            const bool tiedIn = i > 0 && i - 1 < (int) steps->items.size() && Model { steps->items[(std::size_t) i - 1] }.flag ("tie", false);
            if (! on || tiedIn)
                continue;
            int run = 1;
            while (i + run < len && i + run - 1 < (int) steps->items.size() && Model { steps->items[(std::size_t) (i + run - 1)] }.flag ("tie", false))
                ++run;
            const double g = envMean (run * stepSec);
            for (int k = 0; k < run; ++k)
                level[(std::size_t) (i + k)] = g;
        }
        for (int k = 0; k < volumeGateSteps; ++k)
        {
            const double pos = (k + 0.5) / volumeGateSteps * perBar; // the plug-in's step under this one
            const auto i = (std::size_t) ((int) std::floor (pos) % len);
            slot.params[(std::size_t) k] = (float) (1.0 - depth + depth * level[i]);
        }
        raw ("Mix", mix);
        unit ("Attack", 1000.0 * std::min (a, 0.25));
        unit ("Release", 1000.0 * m.num ("release", 0.02));
        return n + volumeGateSteps;
    }
    // round 190nb: the five snapins without a LYNX effect of their own play Kilohearts laws hidden in FL's Effector
    // (core/KiloheartsDsp.h; structures read statically in HeartCore 2.4, measured on kHs Essentials): the visible Type
    // names the nearest Effector algorithm, Wet is the snapin's mix, the law and its values sit in the hidden [12 …]
    if (kind == EffectKind::effector
        && (type == "ring_mod" || type == "reverser" || type == "resonator" || type == "phase_distortion" || type == "shaper"
            || type == "comb_filter" || type == "chorus" || type == "trance_gate" || type == "phaser" || type == "distortion" || type == "bitcrush" || type == "frequency_shifter" || type == "unison"
            || type == "dirty_filter"))
    {
        namespace k = khsdsp;
        auto hidden = [&] (int index, double v) { slot.params[(std::size_t) index] = (float) std::clamp (v, 0.0, 1.0); ++n; };
        auto typeIs = [&] (int effectorType) { raw ("Type", effectorType / 11.0); };
        raw ("Wet", mix);
        raw ("Input", 1.0);
        raw ("Output", 0.5);
        if (type == "ring_mod")
        {
            typeIs (11); // Ring
            hidden (k::lawParam, k::lawValue (k::Law::ringMod));
            hidden (k::valueParam, k::enc::ringHzValue (m.num ("frequency", 440.0)));
            hidden (k::valueParam + 1, m.num ("spread", 0.0));
            hidden (k::valueParam + 2, m.num ("bias", 0.0));
            hidden (k::valueParam + 3, 0.5 * (std::clamp (m.num ("rectify", 0.0), -1.0, 1.0) + 1.0));
        }
        else if (type == "reverser")
        {
            typeIs (9); // Stutter
            hidden (k::lawParam, k::lawValue (k::Law::reverser));
            hidden (k::valueParam, m.num ("delay_time", 0.2) / 5.0);
            hidden (k::valueParam + 1, m.str ("sync_mode") == "ms" ? 0.0 : 1.0);
            hidden (k::valueParam + 2, std::clamp (m.num ("delay_numerator", 4.0), 1.0, 64.0) / 64.0);
            // denominator_4, _4t, _8, _8t, _16, _16t, _32 → 0 … 6
            static constexpr const char* dens[7] { "denominator_4", "denominator_4t", "denominator_8", "denominator_8t",
                                                    "denominator_16", "denominator_16t", "denominator_32" };
            int d = 4;
            for (int i = 0; i < 7; ++i)
                if (m.str ("delay_denominator") == dens[i])
                    d = i;
            hidden (k::valueParam + 3, d / 6.0);
            hidden (k::valueParam + 4, m.num ("crossfade", 0.1));
        }
        else if (type == "resonator")
        {
            typeIs (10); // Vowel (a resonant filter)
            hidden (k::lawParam, k::lawValue (k::Law::resonator));
            hidden (k::valueParam, std::clamp (m.num ("pitch", 69.0), 0.0, 127.0) / 128.0);
            hidden (k::valueParam + 1, k::enc::resonatorDecayValue (m.num ("decay", 0.01)));
            hidden (k::valueParam + 2, m.num ("intensity", 0.5));
            hidden (k::valueParam + 3, m.flag ("harmonics", false) ? 1.0 : 0.0);
        }
        else if (type == "phase_distortion")
        {
            typeIs (0); // Distort
            hidden (k::lawParam, k::lawValue (k::Law::phaseDistortion));
            hidden (k::valueParam, m.num ("drive", 0.5));
            hidden (k::valueParam + 1, 4.0 * std::clamp (m.num ("spread", 0.0), 0.0, 0.25)); // the plug-in's 0 … 0.25 (0 … 90°)
            hidden (k::valueParam + 2, m.num ("normalize", 0.5));
            hidden (k::valueParam + 3, k::enc::toneValue (m.num ("tone", 640.0)));
            hidden (k::valueParam + 4, std::clamp (m.num ("bias", 0.0), -0.5, 0.5) + 0.5);
        }
        else if (type == "comb_filter")
        {
            // round 190nh: CombFilterDsp read in HeartCore (REA_rodada190nh)
            typeIs (5); // Delay (a feed-forward echo)
            hidden (k::lawParam, k::lawValue (k::Law::comb));
            hidden (k::valueParam, k::enc::logValue (m.num ("cutoff", 440.0), k::enc::combLo, k::enc::combHi));
            hidden (k::valueParam + 1, m.flag ("polarity", false) ? 1.0 : 0.0);
            hidden (k::valueParam + 2, m.flag ("stereo", false) ? 1.0 : 0.0);
        }
        else if (type == "chorus")
        {
            typeIs (2); // Flanger (the nearest modulated delay)
            hidden (k::lawParam, k::lawValue (k::Law::chorus));
            hidden (k::valueParam, k::enc::timeValue (m.num ("delay", 0.004)));
            hidden (k::valueParam + 1, k::enc::logValue (m.num ("rate", 0.6), k::enc::rateLo, k::enc::rateHi));
            hidden (k::valueParam + 2, k::enc::timeValue (m.num ("depth", 0.004)));
            hidden (k::valueParam + 3, m.num ("spread", 1.0));
            hidden (k::valueParam + 4, m.flag ("multiTap", false) ? 1.0 : 0.0);
        }
        else if (type == "unison")
        {
            typeIs (2); // Flanger (the nearest modulated delay)
            hidden (k::lawParam, k::lawValue (k::Law::ensemble));
            hidden (k::valueParam, std::clamp (m.num ("voices", 6.0), 2.0, 16.0) / 16.0);
            hidden (k::valueParam + 1, m.num ("detune", 0.25));
            hidden (k::valueParam + 2, m.num ("spread", 0.5));
            const auto mo = m.str ("motion");
            hidden (k::valueParam + 3, mo == "symmetric" ? 0.5 : mo == "sine" ? 1.0 : 0.0);
        }
        else if (type == "frequency_shifter")
        {
            typeIs (11); // Ring (the nearest frequency effect)
            hidden (k::lawParam, k::lawValue (k::Law::freqShift));
            raw ("Wet", 1.0);
            hidden (k::valueParam, std::clamp (m.num ("shift", 0.0), -5000.0, 5000.0) / 10000.0 + 0.5);
        }
        else if (type == "bitcrush")
        {
            typeIs (1); // Lo-fi
            hidden (k::lawParam, k::lawValue (k::Law::bitcrush));
            raw ("Wet", std::clamp (m.num ("mix2", mix), 0.0, 1.0));
            hidden (k::valueParam, k::enc::logValue (m.num ("sample_rate", 6000.0), k::enc::crushLo, k::enc::crushHi));
            hidden (k::valueParam + 1, std::clamp (m.num ("bit_depth", 16.0), 1.0, 16.0) / 16.0);
            hidden (k::valueParam + 2, m.num ("adc_quality", 1.0));
            hidden (k::valueParam + 3, m.num ("dac_quality", 0.0));
            hidden (k::valueParam + 4, m.num ("dither_amount", 0.0));
            hidden (k::valueParam + 5, m.num ("quantize", 1.0));
        }
        else if (type == "distortion")
        {
            typeIs (0); // Distort
            hidden (k::lawParam, k::lawValue (k::Law::distortion));
            hidden (k::valueParam, k::enc::logValue (m.num ("drive", 2.0), k::enc::driveLo, k::enc::driveHi));
            hidden (k::valueParam + 1, 0.5 * (std::clamp (m.num ("bias", 0.0), -1.0, 1.0) + 1.0));
            hidden (k::valueParam + 2, m.num ("spread", 0.0));
            static constexpr const char* types[6] { "overdrive", "saturate", "foldback", "sine", "hardclip", "quantize" };
            int t = 0;
            for (int i = 0; i < 6; ++i)
                if (m.str ("type") == types[i])
                    t = i;
            hidden (k::valueParam + 3, t / 5.0);
            hidden (k::valueParam + 4, m.num ("dynamics", 0.0));
            hidden (k::valueParam + 5, m.flag ("dc_filter", true) ? 1.0 : 0.0);
        }
        else if (type == "dirty_filter")
        {
            // round 190oc: DirtyFilterDsp read in HeartCore (REA_rodada190oc); the type and mode by their saved names
            typeIs (4); // Filter
            hidden (k::lawParam, k::lawValue (k::Law::nonlinearFilter));
            static constexpr const char* types[5] { "low_pass", "band_pass", "high_pass", "notch", "all_pass" };
            static constexpr const char* modes[9] { "clean", "saturated", "tubular", "clipped", "warm", "biased", "fuzzy", "metallic", "digital" };
            int t = 0, d = 1;
            for (int i = 0; i < 5; ++i)
                if (m.str ("filter_type") == types[i])
                    t = i;
            for (int i = 0; i < 9; ++i)
                if (m.str ("dirt_type") == modes[i])
                    d = i;
            hidden (k::valueParam, t / 4.0);
            hidden (k::valueParam + 1, d / 8.0);
            hidden (k::valueParam + 2, k::enc::logValue (m.num ("cutoff", 440.0), k::enc::nlfCutLo, k::enc::nlfCutHi));
            hidden (k::valueParam + 3, k::enc::logValue (m.num ("q", 0.707), k::enc::nlfQLo, k::enc::nlfQHi));
            hidden (k::valueParam + 4, m.num ("drive", 0.25));
        }
        else if (type == "phaser")
        {
            typeIs (3); // Phaser
            hidden (k::lawParam, k::lawValue (k::Law::phaser));
            hidden (k::valueParam, std::clamp (m.num ("order", 3.0), 1.0, 7.0) / 7.0);
            hidden (k::valueParam + 1, std::log (std::clamp (m.num ("cutoff", 500.0), 40.0, 10000.0) / 40.0) / std::log (250.0));
            hidden (k::valueParam + 2, m.num ("depth", 0.5));
            hidden (k::valueParam + 3, k::enc::logValue (m.num ("rate", 0.6), k::enc::rateLo, k::enc::rateHi));
            hidden (k::valueParam + 4, m.num ("spread", 0.5));
        }
        else if (type == "trance_gate")
        {
            typeIs (8); // Trance
            hidden (k::lawParam, k::lawValue (k::Law::tranceGate));
            raw ("Wet", std::clamp (m.num ("depth", 1.0), 0.0, 1.0)); // the Mix knob is the gate's depth
            hidden (k::valueParam, k::enc::timeValue (m.num ("attack", 0.02)));
            hidden (k::valueParam + 1, k::enc::timeValue (m.num ("decay", 0.1)));
            hidden (k::valueParam + 2, m.num ("sustain", 0.5));
            hidden (k::valueParam + 3, k::enc::timeValue (m.num ("release", 0.02)));
            static constexpr const char* rates[7] { "one_8th", "one_8th_t", "one_16th", "one_16th_t", "one_32th", "one_32th_t", "one_64th" };
            int ri = 4;
            for (int i = 0; i < 7; ++i)
                if (m.str ("rate") == rates[i])
                    ri = i;
            hidden (k::valueParam + 4, ri / 6.0);
            hidden (k::valueParam + 5, m.num ("swing", 0.5));
            const auto* pats = m.m.get ("patterns");
            const int sel = std::max (0, (int) std::lround (m.num ("pattern", 1.0)) - 1);
            int len = 16;
            if (pats != nullptr && pats->type == Json::Type::array && ! pats->items.empty())
            {
                const auto& pat = pats->items[(std::size_t) std::min<int> (sel, (int) pats->items.size() - 1)];
                len = std::clamp ((int) std::lround (Model { pat }.num ("length", 16.0)), 1, k::maxGateSteps);
                const auto* steps = pat.get ("steps");
                for (int i = 0; steps != nullptr && steps->type == Json::Type::array && i < k::maxGateSteps && i < (int) steps->items.size(); ++i)
                {
                    const Model st { steps->items[(std::size_t) i] };
                    hidden (k::gateStepParam + i, ((st.flag ("on", false) ? 1 : 0) + (st.flag ("tie", false) ? 2 : 0)) / 3.0);
                }
            }
            hidden (k::valueParam + 6, len / 64.0);
        }
        else // shaper
        {
            typeIs (0); // Distort
            hidden (k::lawParam, k::lawValue (k::Law::shaper));
            hidden (k::valueParam, std::clamp (m.num ("drive", 1.0), 0.0, 10.0) / 10.0);
            hidden (k::valueParam + 1, m.flag ("dcFilter", true) ? 1.0 : 0.0);
            const auto loop = m.str ("loop");
            hidden (k::valueParam + 2, loop == "mirror" ? 0.5 : loop == "repeat" ? 1.0 : 0.0);
            // the curve's points (model.curve.data.segments[k].x / y, −1 … 1), straight lines between them
            const auto* curve = m.m.get ("curve");
            const auto* data = curve != nullptr ? curve->get ("data") : nullptr;
            const auto* segs = data != nullptr ? data->get ("segments") : nullptr;
            int count = 0;
            if (segs != nullptr && segs->type == Json::Type::array)
                for (const auto& s : segs->items)
                {
                    if (count >= k::maxShaperPoints)
                        break;
                    const Model sm { s };
                    hidden (k::shaperPointParam + 2 * count, 0.5 * (std::clamp (sm.num ("x", 0.0), -1.0, 1.0) + 1.0));
                    hidden (k::shaperPointParam + 2 * count + 1, 0.5 * (std::clamp (sm.num ("y", 0.0), -1.0, 1.0) + 1.0));
                    ++count;
                }
            hidden (k::shaperCountParam, count / 16.0);
        }
        return n;
    }
    return n;
}
