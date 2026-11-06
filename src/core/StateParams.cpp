#include "core/StateParams.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>

#include "core/ConvolverFx.h"
#include "core/EquoFx.h"
#include "core/FruityDelayBank.h"
#include "core/MaximusFx.h"
#include "core/MultibandDelayFx.h"
#include "core/Vocodex.h"

namespace lynx::stateparams
{
int Table::index (const char* n) const
{
    if (n == nullptr)
        return -1;
    for (int i = 0; i < (int) params.size(); ++i)
        if (params[(std::size_t) i].name == n)
            return i;
    return -1;
}

const char* Table::name (int i) const
{
    return i >= 0 && i < (int) params.size() ? params[(std::size_t) i].name.c_str() : "";
}

namespace
{
std::string format (const char* f, double v)
{
    char text[48];
    std::snprintf (text, sizeof (text), f, v);
    return text;
}

std::string panText (double v) // −1…1
{
    if (std::abs (v) < 0.005)
        return "Centred";
    return format ("%.0f%%", std::abs (v) * 100.0) + (v < 0 ? " left" : " right");
}

// --- Maximus (core/MaximusFx.h): every control of its bands lives in the state -----------------------------------
Table maximusTable()
{
    Table t;
    for (const auto& p : maximusfx::stateParams())
        t.params.push_back ({ p.name, p.minimum, p.maximum, p.defaultValue });
    t.get = [] (const std::vector<std::uint8_t>& s, int i) { return maximusfx::stateParam (s, i); };
    t.with = [] (const std::vector<std::uint8_t>& s, int i, int raw) { return maximusfx::withStateParam (s, i, raw); };
    t.text = [] (const std::vector<std::uint8_t>&, int i, int raw) { return maximusfx::stateParamText (i, raw); };
    return t;
}

// --- Fruity Delay Bank (core/FruityDelayBank.h): the eight banks' controls and options, the solo and oversampling --
// Index layout: bank b (0…7) owns 27 entries from 27·b: its controls 4 + 16·b + k (k = 0…15; bank 8 has no send),
// then its 11 options; the last two entries are the solo bank and the oversampling.
constexpr int dbankPerBank = 27, dbankControls = 16;
const char* const dbankOptionNames[11] { "Filter 1 type", "Filter 1 order", "Filter 2 type", "Filter 2 order", "Filter 1 post", "Mode",
                                         "Steps", "Shape", "On", "Tap", "Sync" };
const char* const dbankControlNames[16] { "Input pan", "Input volume", "Filter 1 cutoff", "Filter 1 resonance", "Filter 1 gain",
                                          "Feedback pan", "Feedback level", "Feedback cross", "Time", "Stereo offset",
                                          "Filter 2 cutoff", "Filter 2 resonance", "Filter 2 gain", "Output pan", "Output volume",
                                          "Send to next" };

int dbankOption (const fldbank::BankOptions& o, int k)
{
    switch (k)
    {
        case 0: return o.filterType[0];
        case 1: return o.filterOrder[0];
        case 2: return o.filterType[1];
        case 3: return o.filterOrder[1];
        case 4: return o.filterAfter ? 1 : 0;
        case 5: return o.mode;
        case 6: return o.steps;
        case 7: return o.amount;
        case 8: return o.enabled ? 1 : 0;
        case 9: return o.tapBeforeFeedback ? 1 : 0;
        default: return o.sync ? 1 : 0;
    }
}

void setDbankOption (fldbank::BankOptions& o, int k, int v)
{
    switch (k)
    {
        case 0: o.filterType[0] = v; break;
        case 1: o.filterOrder[0] = v; break;
        case 2: o.filterType[1] = v; break;
        case 3: o.filterOrder[1] = v; break;
        case 4: o.filterAfter = v != 0; break;
        case 5: o.mode = v; break;
        case 6: o.steps = v; break;
        case 7: o.amount = v; break;
        case 8: o.enabled = v != 0; break;
        case 9: o.tapBeforeFeedback = v != 0; break;
        default: o.sync = v != 0; break;
    }
}

// the state control of entry i (−1 = an option or a global)
int dbankControlOf (int i)
{
    if (i < 0 || i >= dbankPerBank * fldbank::banks)
        return -1;
    const int b = i / dbankPerBank, k = i % dbankPerBank;
    return k < dbankControls ? 4 + 16 * b + k : -1;
}

Table delayBankTable()
{
    Table t;
    const auto def = fldbank::defaultState();
    // options: filter types 0 off … 7 high shelf, orders 0…2 (one to three sections), mode 0…3, steps 0…16
    // (the upper end of the editor's divider is not confirmed), shape −128…128
    constexpr int optMin[11] { 0, 0, 0, 0, 0, 0, 0, -128, 0, 0, 0 };
    constexpr int optMax[11] { 7, 2, 7, 2, 1, 3, 16, 128, 1, 1, 1 };
    for (int b = 0; b < fldbank::banks; ++b)
    {
        for (int k = 0; k < dbankControls; ++k)
        {
            const int c = 4 + 16 * b + k;
            if (c >= fldbank::automatable) // bank 8 has no "send to next"
                t.params.push_back ({ bankControl (b, "Unused"), 0, 0, 0 });
            else
                t.params.push_back ({ bankControl (b, dbankControlNames[k]), fldbank::controlMin (c), fldbank::controlMax (c), def.p[(std::size_t) c] });
        }
        for (int k = 0; k < 11; ++k)
            t.params.push_back ({ bankControl (b, dbankOptionNames[k]), optMin[k], optMax[k], dbankOption (def.bank[(std::size_t) b], k) });
    }
    t.params.push_back ({ "Solo", -1, fldbank::banks - 1, -1 });
    t.params.push_back ({ "Oversample", 0, 2, 0 });

    const auto n = (int) t.params.size();
    // round 173d: a new Fruity Delay Bank has no state yet (the engine runs FL's defaults): its controls read and edit
    // those defaults (before, every bank control of a new instance did nothing)
    t.get = [n] (const std::vector<std::uint8_t>& bytes, int i) -> std::optional<int> {
        const auto s = bytes.empty() ? std::optional<fldbank::State> (fldbank::defaultState()) : fldbank::parse (bytes);
        if (! s || i < 0 || i >= n)
            return std::nullopt;
        if (const auto c = dbankControlOf (i); c >= 0)
            return c < fldbank::automatable ? s->p[(std::size_t) c] : 0;
        if (i == n - 2)
            return s->p[131];
        if (i == n - 1)
            return s->p[132];
        return dbankOption (s->bank[(std::size_t) (i / dbankPerBank)], i % dbankPerBank - dbankControls);
    };
    t.with = [table = t.params, n] (const std::vector<std::uint8_t>& bytes, int i, int raw) {
        auto s = bytes.empty() ? std::optional<fldbank::State> (fldbank::defaultState()) : fldbank::parse (bytes);
        if (! s || i < 0 || i >= n)
            return bytes;
        raw = std::clamp (raw, table[(std::size_t) i].minimum, table[(std::size_t) i].maximum);
        if (const auto c = dbankControlOf (i); c >= 0)
        {
            if (c >= fldbank::automatable)
                return bytes;
            s->p[(std::size_t) c] = raw;
        }
        else if (i == n - 2)
            s->p[131] = raw;
        else if (i == n - 1)
            s->p[132] = raw;
        else
            setDbankOption (s->bank[(std::size_t) (i / dbankPerBank)], i % dbankPerBank - dbankControls, raw);
        return fldbank::serialise (*s);
    };
    t.text = [n] (const std::vector<std::uint8_t>& bytes, int i, int raw) -> std::string {
        const auto s = fldbank::parse (bytes).value_or (fldbank::defaultState());
        if (const auto c = dbankControlOf (i); c >= 0)
            return fldbank::controlText (s, c, raw);
        if (i == n - 2)
            return raw < 0 ? "None" : format ("Bank %.0f", raw + 1.0);
        if (i == n - 1)
            return raw == 0 ? "Off" : format ("%.0fx", (double) (1 << raw));
        switch (i % dbankPerBank - dbankControls)
        {
            case 0: case 2:
            {
                static const char* const types[8] { "Off", "Low pass", "Band pass", "Notch", "High pass", "Low shelf", "Peaking", "High shelf" };
                return types[std::clamp (raw, 0, 7)];
            }
            case 1: case 3: return format ("%.0f", raw + 1.0) + (raw == 0 ? " section" : " sections");
            case 5:
            {
                static const char* const modes[4] { "Off", "Normal", "Inverted", "Ping-pong" };
                return modes[std::clamp (raw, 0, 3)];
            }
            case 6: return raw == 0 ? "Off" : format ("%.0f", raw);
            case 7: return format ("%+.0f%%", raw * 100.0 / 128.0);
            default: return raw != 0 ? "On" : "Off";
        }
    };
    return t;
}

// --- Multiband Delay (core/MultibandDelayFx.h): 8 banks × (delay, volume, pan) × 16 bands, the editor's selection ---
// Index layout: bank·48 + target·16 + band (FL's own parameter order), then "Target" and "Bank".
Table multibandDelayTable()
{
    Table t;
    const mbdelay::State def;
    static const char* const targets[3] { "delay", "volume", "pan" };
    for (int bank = 0; bank < mbdelay::numBanks; ++bank)
        for (int target = 0; target < 3; ++target)
            for (int band = 0; band < mbdelay::numBands; ++band)
            {
                char name[48];
                std::snprintf (name, sizeof (name), "Bank %d %s %d", bank + 1, targets[target], band + 1);
                t.params.push_back ({ name, 0, 1 << 30, def.raw[(std::size_t) (bank * 48 + target * 16 + band)] });
            }
    t.params.push_back ({ "Target", 0, 2, def.targetSel });
    t.params.push_back ({ "Bank", 0, mbdelay::numBanks - 1, def.bank });
    constexpr int cells = mbdelay::numBanks * 48;

    // round 173d: a new Multiband Delay has no state yet (the engine runs its defaults): the same for its controls
    t.get = [] (const std::vector<std::uint8_t>& bytes, int i) -> std::optional<int> {
        const auto s = bytes.empty() ? std::optional<mbdelay::State> (mbdelay::State {}) : mbdelay::parse (bytes);
        if (! s || i < 0 || i > cells + 1)
            return std::nullopt;
        return i < cells ? s->raw[(std::size_t) i] : i == cells ? s->targetSel : s->bank;
    };
    t.with = [] (const std::vector<std::uint8_t>& bytes, int i, int raw) {
        auto s = bytes.empty() ? std::optional<mbdelay::State> (mbdelay::State {}) : mbdelay::parse (bytes);
        if (! s || i < 0 || i > cells + 1)
            return bytes;
        if (i < cells)
            s->raw[(std::size_t) i] = std::clamp (raw, 0, 1 << 30);
        else if (i == cells)
            s->targetSel = std::clamp (raw, 0, 2);
        else
            s->bank = std::clamp (raw, 0, mbdelay::numBanks - 1);
        return mbdelay::serialize (*s);
    };
    t.text = [] (const std::vector<std::uint8_t>&, int i, int raw) -> std::string {
        if (i == cells)
            return std::array<const char*, 3> { "Delay", "Volume", "Pan" }[(std::size_t) std::clamp (raw, 0, 2)];
        if (i == cells + 1)
            return format ("Bank %.0f", raw + 1.0);
        const auto v = (double) raw / (double) (1 << 30);
        return (i % 48) / 16 == 2 ? panText (v * 2.0 - 1.0) : format ("%.0f%%", v * 100.0);
    };
    return t;
}

// --- EQUO (core/EquoFx.h): the band table, 9 banks (8 + master) × 31 bands × (volume, pan, send), 0…2 floats -----
// Index layout: (bank·31 + band)·3 + target, FL's own table order. Raw values are thousandths.
constexpr std::size_t equoTableAt = 4 + equofx::numBands * equofx::numTargets * 8;
constexpr int equoCells = equofx::numBanks * equofx::numBands * equofx::numTargets;

Table equoTable()
{
    Table t;
    static const char* const targets[3] { "volume", "pan", "send" };
    for (int bank = 0; bank < equofx::numBanks; ++bank)
        for (int band = 0; band < equofx::numBands; ++band)
            for (int target = 0; target < 3; ++target)
            {
                char name[48];
                if (bank == equofx::numBanks - 1)
                    std::snprintf (name, sizeof (name), "Master %s %d", targets[target], band + 1);
                else
                    std::snprintf (name, sizeof (name), "Bank %d %s %d", bank + 1, targets[target], band + 1);
                t.params.push_back ({ name, 0, 2000, 1000 });
            }
    t.get = [] (const std::vector<std::uint8_t>& bytes, int i) -> std::optional<int> {
        if (! equofx::parse (bytes) || i < 0 || i >= equoCells)
            return std::nullopt;
        float v = 0.0f;
        std::memcpy (&v, bytes.data() + equoTableAt + 4 * (std::size_t) i, 4);
        return (int) std::lround (std::clamp (v, 0.0f, 2.0f) * 1000.0f);
    };
    t.with = [] (const std::vector<std::uint8_t>& bytes, int i, int raw) {
        if (! equofx::parse (bytes) || i < 0 || i >= equoCells)
            return bytes;
        auto out = bytes;
        const float v = (float) std::clamp (raw, 0, 2000) / 1000.0f;
        std::memcpy (out.data() + equoTableAt + 4 * (std::size_t) i, &v, 4);
        return out;
    };
    t.text = [] (const std::vector<std::uint8_t>&, int i, int raw) -> std::string {
        const auto v = raw / 1000.0;
        switch (i % 3)
        {
            case 0: return v <= 0.0 ? "-inf dB" : format ("%+.1f dB", 20.0 * std::log10 (v)); // 1 = unity (not confirmed)
            case 1: return panText (v - 1.0);
            default: return format ("%.0f%%", v * 50.0);
        }
    };
    return t;
}

// --- Fruity Convolver (core/ConvolverFx.h): the knobs and the normalise switch of FL's state in the slot blob ------
Table convolverTable()
{
    Table t;
    t.params = { { "Predelay", 0, 1000, 0 }, { "Self-conv", 0, 1000, 0 }, { "Stretch", -1000, 1000, 0 }, { "EQ", 0, 1000, 1000 },
                 { "Normalize", 0, 1, 0 } };
    using Parts = std::pair<std::vector<std::uint8_t>, std::vector<std::uint8_t>>;
    const auto split = [] (const std::vector<std::uint8_t>& blob) -> std::optional<std::pair<Parts, convolver::State>> {
        Parts parts;
        if (! convolver::unpackBlob (blob, parts.first, parts.second))
            return std::nullopt;
        auto s = convolver::parseState (parts.first);
        if (! s)
            return std::nullopt;
        return std::make_pair (std::move (parts), std::move (*s));
    };
    t.get = [split] (const std::vector<std::uint8_t>& blob, int i) -> std::optional<int> {
        const auto p = split (blob);
        if (! p || i < 0 || i > 4)
            return std::nullopt;
        return i == 4 ? (p->second.flags[3] != 0 ? 1 : 0) : p->second.knobs[(std::size_t) i + 1];
    };
    t.with = [split, table = t.params] (const std::vector<std::uint8_t>& blob, int i, int raw) {
        auto p = split (blob);
        if (! p || i < 0 || i > 4)
            return blob;
        auto& fl = p->first.first;
        raw = std::clamp (raw, table[(std::size_t) i].minimum, table[(std::size_t) i].maximum);
        if (i == 4)
        {
            if (p->second.flagsAt == 0)
                return blob;
            fl[p->second.flagsAt + 3] = (std::uint8_t) raw;
        }
        else
        {
            if (p->second.knobsAt == 0 || i + 1 >= p->second.knobCount)
                return blob;
            const auto v = (std::int32_t) raw;
            std::memcpy (fl.data() + p->second.knobsAt + 4 * (std::size_t) (i + 1), &v, 4);
        }
        return convolver::packBlob (fl, p->first.second);
    };
    t.text = [] (const std::vector<std::uint8_t>&, int i, int raw) -> std::string {
        const auto x = raw * 0.001;
        switch (i)
        {
            case 0: return format ("%.1f ms", (std::pow (7.0, x) - 1.0) * 83.333); // REA_rodada60: (7^x − 1)·83.333 ms
            case 1: return format ("%.0f%%", x * 100.0);
            case 2: return format ("%.2fx", std::pow (2.0, x));                    // the sample's speed 2^x
            case 3: return format ("%.0f%%", x * 100.0);
            default: return raw != 0 ? "On" : "Off";
        }
    };
    return t;
}

// --- Vocodex (core/Vocodex.h): FL's 20 controls, the options block and the eight switches, in place in the state -----
// Ranges from FL's form (REA_rodada20 §4.2.2); those of attack / release curve, mod band width, unison pitch and
// unison pan are not confirmed (chosen around the installation presets' values).
struct VcxField
{
    const char* name;
    int minimum, maximum, defaultValue;
    int at;   // byte offset from the controls block
    bool byte = false, inverted = false;
};
const std::vector<VcxField>& vcxFields()
{
    static const std::vector<VcxField> f {
        { "Wet", 0, 2000, 1000, 0 }, { "SG", 0, 1000, 150, 4 }, { "Mod level", 0, 16000, 12800, 8 }, { "Mod HP", 0, 65536, 55000, 12 },
        { "Car level", 0, 16000, 0, 16 }, { "Car LP", 0, 65536, 28000, 20 }, { "Noise", 0, 12800, 0, 24 }, { "Noise HP", 0, 65536, 55000, 28 },
        { "Hold", 0, 10000, 1666, 32 }, { "Attack", 0, 10000, 1115, 36 }, { "Attack curve", 0, 2000, 0, 40 }, { "Release", 0, 10000, 3500, 44 },
        { "Release curve", 0, 2000, 0, 48 }, { "Freeze", 0, 1, 0, 52 }, { "Band width", 5, 1000, 349, 56 }, { "Flatness", 0, 100, 100, 60 },
        { "Mod band width", 0, 4000, 2000, 64 }, { "Mod pitch", -1200, 1200, 0, 68 }, { "Unison pitch", 0, 1000, 250, 72 },
        { "Unison pan", 0, 256, 128, 76 },
        { "Hold mode", 0, 1, 0, 80 }, { "Order", 1, 4, 2, 84 }, { "Unison", 0, 4, 0, 88 }, { "Bands", 5, 100, 47, 92 },
        { "Draft", 0, 1, 1, 96, false, true }, { "Mod input", 0, 8, 1, 100 }, { "Car input", -1, 8, 0, 104 },
        { "L-R encoding", 0, 1, 0, 108, true }, { "Min times", 0, 1, 1, 109, true }, { "To mod", 0, 1, 1, 110, true },
        { "Threaded", 0, 1, 1, 111, true }, { "Subtractive gate", 0, 1, 1, 112, true }, { "To noise", 0, 1, 1, 113, true },
        { "From car", 0, 1, 0, 114, true }, { "From consonants", 0, 1, 1, 115, true } };
    return f;
}

// byte offset of the controls block in a slot state (FL's state + LYNX's install-data trailer); 0 = not a Vocodex state
std::size_t vcxBlock (const std::vector<std::uint8_t>& slotState)
{
    const auto fl = vocodex::splitInstallData (slotState).first;
    const auto s = vocodex::parseState (fl);
    return s && s->paramsAt > 0 && s->paramsAt + 116 <= fl.size() ? s->paramsAt : 0;
}

Table vocodexTable()
{
    Table t;
    for (const auto& f : vcxFields())
        t.params.push_back ({ f.name, f.minimum, f.maximum, f.defaultValue });
    t.get = [] (const std::vector<std::uint8_t>& bytes, int i) -> std::optional<int> {
        const auto& fs = vcxFields();
        const auto at = vcxBlock (bytes);
        if (at == 0 || i < 0 || i >= (int) fs.size())
            return std::nullopt;
        const auto& f = fs[(std::size_t) i];
        std::int32_t v = 0;
        if (f.byte)
            v = bytes[at + (std::size_t) f.at] != 0 ? 1 : 0;
        else
            std::memcpy (&v, bytes.data() + at + (std::size_t) f.at, 4);
        return f.inverted ? (v == 0 ? 1 : 0) : v;
    };
    t.with = [] (const std::vector<std::uint8_t>& bytes, int i, int raw) {
        const auto& fs = vcxFields();
        const auto at = vcxBlock (bytes);
        if (at == 0 || i < 0 || i >= (int) fs.size())
            return bytes;
        const auto& f = fs[(std::size_t) i];
        raw = std::clamp (raw, f.minimum, f.maximum);
        auto out = bytes;
        if (f.inverted)
            raw = raw != 0 ? 0 : 1;
        if (f.byte)
            out[at + (std::size_t) f.at] = (std::uint8_t) (raw != 0 ? 1 : 0);
        else
        {
            const std::int32_t v = raw;
            std::memcpy (out.data() + at + (std::size_t) f.at, &v, 4);
        }
        return out;
    };
    t.text = [] (const std::vector<std::uint8_t>&, int i, int raw) -> std::string {
        const auto db = [] (double g) { return g <= 1e-9 ? std::string ("-inf dB") : format ("%+.1f dB", 20.0 * std::log10 (g)); };
        const auto hz = [] (int v) { return format ("%.0f Hz", 40.0 * std::pow (375.0, v / 65536.0)); };
        const auto ms = [] (int v, double k) { return format ("%.1f ms", (std::pow (501.0, v / 10000.0) - 1.0) * k); };
        switch (i)
        {
            case 0: return db ((std::pow (7.0, raw / 1000.0) - 1.0) / 6.0);
            case 1: return format ("%.0f%%", raw / 10.0);
            case 2: case 4: return db ((std::pow (11.0, raw / 12800.0) - 1.0) / 10.0);
            case 3: case 7: return raw == 0 ? "Off" : hz (raw);
            case 5: return raw >= 65536 ? "Off" : hz (raw);
            case 6: return db (0.5 * (std::pow (7.0, raw / 12800.0) - 1.0) / 6.0);
            case 8: return ms (raw, 0.5);
            case 9: return ms (raw, 2.0);
            case 11: return ms (raw, 4.0);
            case 10: case 12: return format ("x%.2f", std::pow (2.0, raw / 1000.0));
            case 14: return format ("%.2f", 0.5 * (std::pow (5.0, raw / 1000.0) - 1.0));
            case 15: return format ("%.0f%%", (double) raw);
            case 16: return format ("x%.2f", std::pow (2.0, raw / 1000.0) / 4.0);
            case 17: return format ("%+.0f ct", (double) raw);
            case 18: return format ("%.3f oct", raw / 1000.0);
            case 19: return format ("%.0f%%", raw * 100.0 / 128.0);
            case 20: return raw != 0 ? "RMS" : "Peak";
            case 21: case 23: return format ("%.0f", (double) raw);
            case 22: return format ("%.0f", raw + 1.0); // FL shows U + 1
            case 25: return format ("%.0f", (double) raw); // 0 = own input, k = side input k
            case 26: return raw < 0 ? "Synth" : format ("%.0f", (double) raw);
            default: return raw != 0 ? "On" : "Off";
        }
    };
    return t;
}
} // namespace

std::string bankControl (int bank, const char* control)
{
    return "B" + std::to_string (bank + 1) + " " + control;
}

const Table* tableFor (EffectKind kind)
{
    static std::mutex lock;
    static std::map<EffectKind, Table> tables;
    const std::lock_guard<std::mutex> guard (lock);
    if (auto it = tables.find (kind); it != tables.end())
        return &it->second;
    Table t;
    switch (kind)
    {
        case EffectKind::maximus: t = maximusTable(); break;
        case EffectKind::fruityDelayBank: t = delayBankTable(); break;
        case EffectKind::multibandDelay: t = multibandDelayTable(); break;
        case EffectKind::equo: t = equoTable(); break;
        case EffectKind::convolver: t = convolverTable(); break;
        case EffectKind::vocodex: t = vocodexTable(); break;
        default: return nullptr;
    }
    return &tables.emplace (kind, std::move (t)).first->second;
}
} // namespace lynx::stateparams
