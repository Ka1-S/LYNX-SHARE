#include "core/Slicex.h"

#include <algorithm>
#include <cmath>
#include <cstring>

// FL's Slicex (Slicex_x64.dll; docs/analise/REA_rodada190h.md): the state codec, the WAV regions, the key map and the
// parameter laws.

namespace lynx::slicex
{
namespace
{
struct Reader
{
    const std::vector<std::uint8_t>& d;
    std::size_t pos = 0;
    bool ok = true;

    const std::uint8_t* raw (std::size_t n) noexcept
    {
        if (! ok || n > d.size() - pos)
        {
            ok = false;
            return nullptr;
        }
        const auto* p = d.data() + pos;
        pos += n;
        return p;
    }
    template <typename T>
    T get() noexcept
    {
        T v {};
        if (const auto* p = raw (sizeof (T)))
            std::memcpy (&v, p, sizeof (T));
        return v;
    }
    std::size_t left() const noexcept { return d.size() - pos; }
};

struct Writer
{
    std::vector<std::uint8_t> out;

    void raw (const void* p, std::size_t n)
    {
        const auto* b = static_cast<const std::uint8_t*> (p);
        out.insert (out.end(), b, b + n);
    }
    template <typename T>
    void put (T v) { raw (&v, sizeof (T)); }
};

// the curve record: kind, version, the points (24 bytes each), then flags, enabled, decay, loop, sustain
bool readCurve (Reader& r, Curve& c, int kind)
{
    c.kind = r.get<std::int32_t>();
    c.version = r.get<std::int32_t>();
    const auto n = r.get<std::int32_t>();
    // (FL reads older point layouts too; every Slicex state it writes holds version 3, the only one written back)
    if (! r.ok || c.kind != kind || c.version != 3 || n < 0 || (std::size_t) n > r.left() / 24)
        return false;
    c.points.resize ((std::size_t) n);
    for (auto& p : c.points)
    {
        p.dt = r.get<double>();
        p.y = r.get<double>();
        p.tension = r.get<float>();
        p.mode = r.get<std::uint8_t>();
        p.b1 = r.get<std::uint8_t>();
        p.b2 = r.get<std::uint8_t>();
        p.b3 = r.get<std::uint8_t>();
    }
    c.flags = r.get<std::int32_t>();
    c.enabled = r.get<std::int32_t>();
    c.decay = r.get<std::int32_t>();
    c.loop = r.get<std::int32_t>();
    c.sustain = r.get<std::int32_t>();
    return r.ok;
}

void writeCurve (Writer& w, const Curve& c)
{
    w.put (c.kind);
    w.put (c.version);
    w.put ((std::int32_t) c.points.size());
    for (const auto& p : c.points)
    {
        w.put (p.dt);
        w.put (p.y);
        w.put (p.tension);
        w.put (p.mode);
        w.put (p.b1);
        w.put (p.b2);
        w.put (p.b3);
    }
    w.put (c.flags);
    w.put (c.enabled);
    w.put (c.decay);
    w.put (c.loop);
    w.put (c.sustain);
}

bool readArticulator (Reader& r, Articulator& a)
{
    a.version = r.get<std::int32_t>();
    a.base = r.get<float>();
    a.envDepth = r.get<float>();
    a.lfoDepth = r.get<float>();
    if (! readCurve (r, a.envelope, 1))
        return false;
    for (auto& k : a.envKnobs)
        k = r.get<std::int32_t>();
    if (! readCurve (r, a.lfo, 2))
        return false;
    for (auto& k : a.lfoKnobs)
        k = r.get<std::int32_t>();
    for (auto& m : a.maps)
        if (! readCurve (r, m, 3))
            return false;
    return r.ok;
}

void writeArticulator (Writer& w, const Articulator& a)
{
    w.put (a.version);
    w.put (a.base);
    w.put (a.envDepth);
    w.put (a.lfoDepth);
    writeCurve (w, a.envelope);
    for (const auto k : a.envKnobs)
        w.put (k);
    writeCurve (w, a.lfo);
    for (const auto k : a.lfoKnobs)
        w.put (k);
    for (const auto& m : a.maps)
        writeCurve (w, m);
}

// FL folds 0x10000…0x60003 to its high word; a plain small number (the plugin database's copy) is the same version
std::uint32_t foldVersion (std::uint32_t v) noexcept { return v > 0xffffu && v < 0x60004u ? v >> 16 : v; }

Point point (double dt, double y, float tension = 0.0f, std::uint8_t b3 = 0) noexcept
{
    Point p;
    p.dt = dt;
    p.y = y;
    p.tension = tension;
    p.b3 = b3;
    return p;
}

Curve flatMap (double level)
{
    Curve c;
    c.kind = 3;
    c.points = { point (0.0, level), point (1.0, level, 0.0f, 2) };
    c.enabled = -1;
    return c;
}

const float ln11 = (float) std::log (11.0);
const float ln3 = (float) std::log (3.0);

float law11 (float x) noexcept
{
    return x == 1.0f ? 1.0f : (float) ((std::exp ((double) (x * ln11)) - 1.0) * (double) 0.1f);
}

std::uint32_t le32 (const std::uint8_t* p) noexcept
{
    std::uint32_t v;
    std::memcpy (&v, p, 4);
    return v;
}
} // namespace

RegionRecord defaultRegion() noexcept
{
    return { 0, 0, 0, 8000, 0, 12800, 128, 128, 0, 12800, 0, 0, 12800, 0, 0, 0, 12800 };
}

RegionValues regionValues (const RegionRecord& r) noexcept
{
    RegionValues v;
    v.cutGroup = r[rCutGroup];
    v.output = r[rOutput];
    v.ampGroup = r[rAmpGroup];
    v.filterGroup = r[rFilterGroup];
    v.speedGroup = r[rSpeedGroup];
    v.startGroup = r[rStartGroup];
    v.pan = (float) r[rPan] * 0.00015625f;
    v.volume = regionVolume (r[rVolume]);
    v.ampAmount = (float) r[rAmpAmount] * 7.8125e-05f;
    v.cutoff = (float) r[rCutoff] * 0.00390625f;
    v.resonance = (float) r[rResonance] * 0.00390625f;
    v.filterAmount = (float) r[rFilterAmount] * 7.8125e-05f;
    v.pitchCents = (float) r[rPitch];
    v.speedAmount = (float) r[rSpeedAmount] * 7.8125e-05f * 8.0f;
    v.delayBeats = (float) r[rDelay] * 7.8125e-05f;
    v.start = (float) r[rStart] * 7.8125e-05f;
    v.startAmount = (float) r[rStartAmount] * 7.8125e-05f * 2.0f;
    return v;
}

double Deck::beatStart() const noexcept
{
    double v;
    std::memcpy (&v, params.data() + 0x10, 8);
    return v;
}

double Deck::beatEnd() const noexcept
{
    double v;
    std::memcpy (&v, params.data() + 0x18, 8);
    return v;
}

double Deck::beats() const noexcept
{
    if (const auto own = wavBeats (wav); own > 0.0)
        return own;
    const auto shown = beatEnd() - beatStart();
    return std::isfinite (shown) && shown > 0.0 ? shown : 0.0;
}

double Deck::selectionStart() const noexcept
{
    double v;
    std::memcpy (&v, params.data(), 8);
    return v;
}

double Deck::selectionEnd() const noexcept
{
    double v;
    std::memcpy (&v, params.data() + 8, 8);
    return v;
}

bool Deck::hasSelection() const noexcept
{
    const auto a = selectionStart(), b = selectionEnd();
    return std::isfinite (a) && std::isfinite (b) && a >= 0.0 && b > a;
}

std::int32_t State::optionInt (int offset) const noexcept
{
    std::int32_t v = 0;
    if (offset >= 0 && (std::size_t) offset + 4 <= options.size())
        std::memcpy (&v, options.data() + offset, 4);
    return v;
}

void State::setOptionInt (int offset, std::int32_t v) noexcept
{
    if (offset >= 0 && (std::size_t) offset + 4 <= options.size())
        std::memcpy (options.data() + offset, &v, 4);
}

std::uint8_t State::optionByte (int offset) const noexcept
{
    return offset >= 0 && (std::size_t) offset < options.size() ? options[(std::size_t) offset] : (std::uint8_t) 0;
}

void State::setOptionByte (int offset, std::uint8_t v) noexcept
{
    if (offset >= 0 && (std::size_t) offset < options.size())
        options[(std::size_t) offset] = v;
}

std::optional<State> parse (const std::vector<std::uint8_t>& bytes)
{
    Reader r { bytes };
    State s;
    s.version = r.get<std::uint32_t>();
    const auto v = foldVersion (s.version);
    if (! r.ok || v < 1 || v > 6)
        return std::nullopt;
    for (auto& m : s.masters)
        m = r.get<std::int32_t>();
    const std::size_t optionBytes = v < 2 ? 46 : 47;
    if (const auto* p = r.raw (optionBytes))
        s.options.assign (p, p + optionBytes);
    for (auto& u : s.ui)
        u = r.get<std::int32_t>();
    for (auto& d : s.decks)
    {
        d.referenceOnly = v >= 2 ? r.get<std::uint8_t>() : (std::uint8_t) 0;
        d.hasPath = d.referenceOnly != 0 || v > 2;
        d.longPath = false;
        d.path.clear();
        if (d.hasPath)
        {
            std::size_t n = r.get<std::uint8_t>();
            if (n == 255)
            {
                d.longPath = true;
                n = r.get<std::uint32_t>();
            }
            if (const auto* p = r.raw (n))
                d.path.assign (reinterpret_cast<const char*> (p), n);
        }
        if (d.referenceOnly == 0)
        {
            const auto n = r.get<std::int32_t>();
            if (n > 0)
                if (const auto* p = r.raw ((std::size_t) n))
                    d.wav.assign (p, p + n);
        }
        if (const auto* p = r.raw (d.params.size()))
            std::memcpy (d.params.data(), p, d.params.size());
        for (auto& c : d.curves)
            if (! readCurve (r, c, 6))
                return std::nullopt;
        const auto n = r.get<std::int32_t>();
        if (! r.ok || n < 0 || (std::size_t) n > r.left() / 68)
            return std::nullopt;
        d.regions.resize ((std::size_t) n);
        for (auto& rec : d.regions)
            for (auto& f : rec)
                f = r.get<std::int32_t>();
    }
    for (auto& g : s.groups)
    {
        for (auto& h : g.header)
            h = r.get<std::int32_t>();
        for (auto& a : g.art)
            if (! readArticulator (r, a))
                return std::nullopt;
    }
    if (! r.ok)
        return std::nullopt;
    s.trailer.assign (bytes.begin() + (std::ptrdiff_t) r.pos, bytes.end());
    return s;
}

std::vector<std::uint8_t> write (const State& s)
{
    Writer w;
    const auto v = foldVersion (s.version);
    w.put (s.version);
    for (const auto m : s.masters)
        w.put (m);
    w.raw (s.options.data(), s.options.size());
    for (const auto u : s.ui)
        w.put (u);
    for (const auto& d : s.decks)
    {
        if (v >= 2)
            w.put (d.referenceOnly);
        if (d.hasPath)
        {
            if (d.longPath || d.path.size() >= 255)
            {
                w.put ((std::uint8_t) 255);
                w.put ((std::uint32_t) d.path.size());
            }
            else
                w.put ((std::uint8_t) d.path.size());
            w.raw (d.path.data(), d.path.size());
        }
        if (d.referenceOnly == 0)
        {
            w.put ((std::int32_t) d.wav.size());
            w.raw (d.wav.data(), d.wav.size());
        }
        w.raw (d.params.data(), d.params.size());
        for (const auto& c : d.curves)
            writeCurve (w, c);
        w.put ((std::int32_t) d.regions.size());
        for (const auto& rec : d.regions)
            for (const auto f : rec)
                w.put (f);
    }
    for (const auto& g : s.groups)
    {
        for (const auto h : g.header)
            w.put (h);
        for (const auto& a : g.art)
            writeArticulator (w, a);
    }
    w.raw (s.trailer.data(), s.trailer.size());
    return std::move (w.out);
}

State fresh()
{
    State s;
    // the options FL's constructor sets: unbalanced crossfade, volume as the articulator target shown, no auto scroll,
    // smooth modulation, auto dump, dual deck, velocity linked to volume, keyboard shown
    s.options.assign (47, 0);
    s.setOptionInt (oCrossfade, 3);
    s.setOptionInt (0x08, 5);
    s.setOptionInt (0x0c, 3);
    s.setOptionInt (oArtTarget, 1);
    s.setOptionByte (oNoAutoScroll, 1);
    s.setOptionByte (oModSmooth, 1);
    s.setOptionByte (oAutoDump, 1);
    s.setOptionByte (oDualDeck, 1);
    s.setOptionByte (oVelocityToVolume, 1);
    s.setOptionByte (oKeyboard, 1);

    for (auto& d : s.decks)
    {
        // no selection, a beat grid of 0…(111.5 frames at 44.1 kHz, FL's empty sample), noise threshold 0.02, 512
        // spectrum bands, scope channel mode 1, points / regions / scroller shown
        const double sel = -1.0, beat0 = 0.0, beat1 = 0.0025283446712018143;
        const float noise = 0.02f;
        const std::int32_t ints[4] = { 0, 512, 1, -1 };
        std::memcpy (d.params.data() + 0x00, &sel, 8);
        std::memcpy (d.params.data() + 0x08, &sel, 8);
        std::memcpy (d.params.data() + 0x10, &beat0, 8);
        std::memcpy (d.params.data() + 0x18, &beat1, 8);
        std::memcpy (d.params.data() + 0x20, &noise, 4);
        std::memcpy (d.params.data() + 0x24, ints, 16);
        d.params[0x34] = 1;
        d.params[0x35] = 1;
        d.params[0x37] = 1;
        for (auto& c : d.curves)
        {
            c.kind = 6;
            c.points = { point (0.0, 0.5), point (2.0 / 44100.0, 0.5, 0.0f, 2) };
            c.enabled = -1;
        }
        d.regions = { defaultRegion() };
    }

    Group g;
    for (int k = 0; k < targetCount; ++k)
    {
        auto& a = g.art[(std::size_t) k];
        const bool multiplies = k == volume || k == speed; // (their maps rest at 1, the others at 0.5)
        a.envelope.kind = 1;
        a.lfo.kind = 2;
        a.lfo.flags = 3;
        a.lfo.points = { point (0.0, 1.0) };
        for (auto& m : a.maps)
            m = flatMap (multiplies ? 1.0 : 0.5);
        switch (k)
        {
            case volume:
            case cutoff:
                a.envelope.points = { point (0.0, 1.0), point (1.0, 0.0, 0.1750001f, 1) };
                a.envelope.sustain = 1;
                break;
            case speed:
                a.envelope.points = { point (0.0, 0.5), point (1.2000000000000002, 0.0, 0.2f, 1) };
                a.envelope.sustain = 1;
                break;
            case sampleStart:
                a.envelope.points = { point (0.0, 0.0), point (0.75, 1.0, 0.2f, 1), point (1.5, 0.5, 0.2f, 1), point (0.25, 0.0, 0.2f, 1) };
                a.envelope.decay = 1;
                a.envelope.sustain = 2;
                a.lfo.flags = 0;
                a.lfo.points = { point (0.0, 0.5), point (1.0, 0.5, 0.0f, 2), point (0.25, 1.0, 0.0f, 2), point (1.0, 0.5, 0.0f, 2) };
                a.lfo.sustain = 2;
                a.lfoKnobs[0] = 40000;
                break;
            default: // pan, resonance
                a.envelope.points = { point (0.0, 0.5) };
                break;
        }
    }
    s.groups.fill (g);
    return s;
}

void setDeckFile (State& state, int deck, const std::string& path, int regions)
{
    if (deck < 0 || deck > 1)
        return;
    auto& d = state.decks[(std::size_t) deck];
    d.referenceOnly = 1;
    d.hasPath = true;
    d.longPath = false;
    d.path = path;
    d.wav.clear();
    d.regions.assign ((std::size_t) std::max (1, regions), defaultRegion());
}

void setDeckBeats (State& state, int deck, double start, double end)
{
    if (deck < 0 || deck > 1)
        return;
    auto& d = state.decks[(std::size_t) deck];
    std::memcpy (d.params.data() + 0x10, &start, 8);
    std::memcpy (d.params.data() + 0x18, &end, 8);
}

double deckBeats (const Deck& deck, double fileBeats) noexcept
{
    if (const auto own = wavBeats (deck.wav); own > 0.0)
        return own;
    if (deck.wav.empty() && std::isfinite (fileBeats) && fileBeats > 0.0)
        return fileBeats;
    return deck.beats();
}

void setDeckSelection (State& state, int deck, double from, double to)
{
    if (deck < 0 || deck > 1)
        return;
    if (from > to)
        std::swap (from, to);
    if (! (to > from) || from < 0.0)
        from = to = -1.0;
    auto& d = state.decks[(std::size_t) deck];
    std::memcpy (d.params.data(), &from, 8);
    std::memcpy (d.params.data() + 8, &to, 8);
}

FrameRange framesOf (double fromBeats, double toBeats, double framesPerBeat, std::int64_t frames) noexcept
{
    FrameRange r;
    if (! (framesPerBeat > 0.0) || frames <= 0 || ! std::isfinite (fromBeats) || ! std::isfinite (toBeats))
        return r;
    if (fromBeats > toBeats)
        std::swap (fromBeats, toBeats);
    r.first = std::clamp<std::int64_t> (std::llround (fromBeats * framesPerBeat), 0, frames);
    r.end = std::clamp<std::int64_t> (std::llround (toBeats * framesPerBeat), r.first, frames);
    return r;
}

double wavBeats (const std::vector<std::uint8_t>& w) noexcept
{
    if (w.size() < 12 || (std::memcmp (w.data(), "RIFF", 4) != 0 && std::memcmp (w.data(), "RF64", 4) != 0))
        return 0.0;
    for (std::size_t p = 12; p + 8 <= w.size();)
    {
        std::uint32_t len = 0;
        std::memcpy (&len, w.data() + p + 4, 4);
        const auto body = p + 8;
        if (std::memcmp (w.data() + p, "data", 4) == 0 && (len == 0xffffffffu || body + len > w.size()))
            break; // (a 64-bit or cut file: nothing after the samples to read)
        if (body + len > w.size())
            break;
        if (std::memcmp (w.data() + p, "acid", 4) == 0 && len >= 16)
        {
            std::uint32_t beats = 0;
            std::memcpy (&beats, w.data() + body + 12, 4);
            return beats > 0 && beats < 65536 ? (double) beats : 0.0;
        }
        p = body + len + (len & 1);
    }
    return 0.0;
}

double guessBeats (double seconds) noexcept
{
    if (! (seconds > 0.0) || ! std::isfinite (seconds))
        return 0.0;
    double beats = 1.0;
    while (beats * 60.0 / seconds <= 80.0 && beats < 65536.0)
        beats *= 2.0;
    while (beats * 60.0 / seconds > 160.0 && beats > 1.0 / 65536.0)
        beats *= 0.5;
    return beats;
}

float masterVolume (std::int32_t raw) noexcept { return law11 ((float) std::clamp (raw, 0, 16000) * 7.8125e-05f); }

float masterBipolar (std::int32_t raw, int unit) noexcept
{
    const float x = (float) raw / (float) unit;
    const float g = law11 (std::abs (x));
    return x < 0.0f ? -g : g;
}

float regionVolume (std::int32_t raw) noexcept
{
    const float x = (float) raw * 0.000125f;
    return x == 1.0f ? 1.0f : (float) ((std::exp ((double) (x * ln3)) - 1.0) * 0.5);
}

std::vector<WavRegion> wavRegions (const std::vector<std::uint8_t>& w, std::int64_t& frames)
{
    frames = 0;
    std::vector<WavRegion> regions;
    if (w.size() < 12 || (std::memcmp (w.data(), "RIFF", 4) != 0 && std::memcmp (w.data(), "RF64", 4) != 0))
        return regions;

    struct Cue
    {
        std::uint32_t id = 0;
        std::int64_t start = 0, length = 0;
        std::string name, info, purpose;
        int key = -1;
    };
    std::vector<Cue> cues;
    struct Extra
    {
        std::uint32_t id = 0;
        int what = 0; // 0 name, 1 length (text: its purpose), 2 key, 3 info
        std::string text;
        std::int64_t value = 0;
    };
    std::vector<Extra> extras;
    std::size_t blockAlign = 1;
    bool packed = false;
    std::int64_t dataFrames = 0, factFrames = -1;

    for (std::size_t p = 12; p + 8 <= w.size();)
    {
        const std::size_t size = le32 (w.data() + p + 4);
        const auto* body = w.data() + p + 8;
        const std::size_t have = std::min (size, w.size() - (p + 8));
        if (std::memcmp (w.data() + p, "fmt ", 4) == 0 && have >= 14)
        {
            blockAlign = std::max<std::size_t> (1, (std::size_t) body[12] | ((std::size_t) body[13] << 8));
            const auto tag = (unsigned) body[0] | ((unsigned) body[1] << 8);
            packed = tag != 1 && tag != 3 && tag != 0xfffe; // not PCM / float (FL's own packs are Ogg Vorbis in a WAV, tag 0x674f)
        }
        else if (std::memcmp (w.data() + p, "fact", 4) == 0 && have >= 4)
            factFrames = (std::int64_t) le32 (body);
        else if (std::memcmp (w.data() + p, "data", 4) == 0)
            dataFrames = (std::int64_t) (have / blockAlign);
        else if (std::memcmp (w.data() + p, "cue ", 4) == 0 && have >= 4)
        {
            const std::size_t n = std::min<std::size_t> (le32 (body), (have - 4) / 24);
            for (std::size_t k = 0; k < n; ++k)
            {
                Cue c;
                c.id = le32 (body + 4 + 24 * k);
                c.start = (std::int64_t) le32 (body + 4 + 24 * k + 20); // the sample offset
                cues.push_back (c);
            }
        }
        else if (std::memcmp (w.data() + p, "tlst", 4) == 0 && have >= 4)
        {
            // FL's trigger list, a chunk of its own after LIST/adtl (round 190r: a project FL saved after "Assign
            // current region to this key"; a "tlst" inside a LIST is ignored by FL): i32 count, then 24-byte entries
            // {'cue ', cue id − 1, 1, 0xFF, key, 0x90, 0, 0, 0}. LYNX 0.1.27's Edison wrote {0, cue id} (round 190gk
            // fixed it to FL's layout): an entry whose second word is not FL's 1 is read that way.
            const std::size_t n = std::min<std::size_t> (le32 (body), (have - 4) / 24);
            for (std::size_t k = 0; k < n; ++k)
            {
                const auto* e = body + 4 + 24 * k;
                const auto second = le32 (e + 8);
                extras.push_back ({ second == 1 ? le32 (e + 4) + 1 : second, 2, {}, (std::int64_t) e[13] });
            }
        }
        else if (std::memcmp (w.data() + p, "LIST", 4) == 0 && have >= 4)
        {
            if (std::memcmp (body, "adtl", 4) == 0)
            {
                for (std::size_t q = 4; q + 8 <= have;)
                {
                    const std::size_t sub = std::min<std::size_t> (le32 (body + q + 4), have - (q + 8));
                    const auto* sb = body + q + 8;
                    if (sub >= 4)
                    {
                        const auto id = le32 (sb);
                        if (std::memcmp (body + q, "labl", 4) == 0)
                        {
                            const auto* text = reinterpret_cast<const char*> (sb + 4);
                            extras.push_back ({ id, 0, std::string (text, strnlen (text, sub - 4)), 0 });
                        }
                        else if (std::memcmp (body + q, "note", 4) == 0)
                        {
                            const auto* text = reinterpret_cast<const char*> (sb + 4);
                            extras.push_back ({ id, 3, std::string (text, strnlen (text, sub - 4)), 0 });
                        }
                        else if (std::memcmp (body + q, "ltxt", 4) == 0 && sub >= 8)
                            extras.push_back ({ id, 1, sub >= 12 ? std::string (reinterpret_cast<const char*> (sb + 8), 4) : std::string(), (std::int64_t) le32 (sb + 4) });
                    }
                    q += 8 + sub + (sub & 1);
                }
            }
        }
        p += 8 + size + (size & 1);
    }
    // a packed WAV's length is in its fact chunk (the data chunk holds the packed stream)
    frames = packed ? std::max<std::int64_t> (0, factFrames) : dataFrames;

    for (const auto& e : extras)
        for (auto& c : cues)
            if (c.id == e.id)
            {
                if (e.what == 0)
                    c.name = e.text;
                else if (e.what == 1)
                {
                    c.length = e.value;
                    c.purpose = e.text;
                }
                else if (e.what == 3)
                    c.info = e.text;
                else
                    c.key = (int) e.value;
            }
    std::stable_sort (cues.begin(), cues.end(), [] (const Cue& a, const Cue& b) { return a.start < b.start; });

    regions.reserve (cues.size());
    for (const auto& c : cues)
    {
        WavRegion r;
        r.start = c.start;
        r.name = c.name;
        r.info = c.info;
        r.purpose = c.purpose;
        r.key = c.key >= 0 && c.key <= 127 ? c.key : -1;
        r.marker = c.length <= 0;
        r.end = r.marker ? -1 : c.start + c.length - 1;
        regions.push_back (std::move (r));
    }
    // a marker ends before the next region that starts later, the last one at the sample's end
    for (std::size_t i = 0; i < regions.size(); ++i)
        if (regions[i].marker)
        {
            regions[i].end = frames - 1;
            for (std::size_t j = i + 1; j < regions.size(); ++j)
                if (regions[j].start > regions[i].start)
                {
                    regions[i].end = regions[j].start - 1;
                    break;
                }
        }
    return regions;
}

std::vector<std::uint8_t> withRegionKeys (const std::vector<std::uint8_t>& w, const std::vector<int>& keys)
{
    if (w.size() < 12 || std::memcmp (w.data(), "RIFF", 4) != 0 || std::memcmp (w.data() + 8, "WAVE", 4) != 0)
        return {};
    // the cue points in the order wavRegions() lists the regions (by start, file order kept), and where the list goes:
    // after LIST/adtl, else after the cue chunk (FL's order: … cue, LIST/adtl, tlst, LIST/INFO)
    std::vector<std::pair<std::int64_t, std::uint32_t>> cues; // start, id
    std::vector<std::uint8_t> out (w.begin(), w.begin() + 12);
    std::size_t at = 0;
    for (std::size_t p = 12; p + 8 <= w.size();)
    {
        const std::size_t size = le32 (w.data() + p + 4);
        const std::size_t have = std::min (size, w.size() - (p + 8));
        const auto* body = w.data() + p + 8;
        const std::size_t next = std::min (w.size(), p + 8 + size + (size & 1));
        const bool list = std::memcmp (w.data() + p, "tlst", 4) == 0;
        if (std::memcmp (w.data() + p, "cue ", 4) == 0 && have >= 4)
        {
            const std::size_t n = std::min<std::size_t> (le32 (body), (have - 4) / 24);
            for (std::size_t k = 0; k < n; ++k)
                cues.push_back ({ (std::int64_t) le32 (body + 4 + 24 * k + 20), le32 (body + 4 + 24 * k) });
        }
        if (! list) // (the old list is dropped)
            out.insert (out.end(), w.begin() + (std::ptrdiff_t) p, w.begin() + (std::ptrdiff_t) next);
        if (std::memcmp (w.data() + p, "cue ", 4) == 0 || (std::memcmp (w.data() + p, "LIST", 4) == 0 && have >= 4 && std::memcmp (body, "adtl", 4) == 0))
            at = out.size();
        p = next;
    }
    if (cues.empty() || at == 0)
        return {};
    std::stable_sort (cues.begin(), cues.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<std::uint8_t> list;
    auto put32 = [] (std::vector<std::uint8_t>& o, std::uint32_t v) {
        for (int i = 0; i < 4; ++i)
            o.push_back ((std::uint8_t) (v >> (8 * i)));
    };
    std::uint32_t count = 0;
    for (std::size_t i = 0; i < cues.size() && i < keys.size(); ++i)
        count += keys[i] >= 0 && keys[i] <= 127 ? 1u : 0u;
    if (count > 0)
    {
        list.insert (list.end(), { 't', 'l', 's', 't' });
        put32 (list, 4 + 24 * count);
        put32 (list, count);
        for (std::size_t i = 0; i < cues.size() && i < keys.size(); ++i)
            if (keys[i] >= 0 && keys[i] <= 127)
            {
                list.insert (list.end(), { 'c', 'u', 'e', ' ' });
                put32 (list, cues[i].second - 1);
                put32 (list, 1);
                list.insert (list.end(), { 0xff, (std::uint8_t) keys[i], 0x90, 0 });
                put32 (list, 0);
                put32 (list, 0);
            }
    }
    out.insert (out.begin() + (std::ptrdiff_t) at, list.begin(), list.end());
    const auto riff = (std::uint32_t) (out.size() - 8);
    for (int i = 0; i < 4; ++i)
        out[4 + (std::size_t) i] = (std::uint8_t) (riff >> (8 * i));
    return out;
}

std::vector<std::uint8_t> withRegions (const std::vector<std::uint8_t>& w, const std::vector<WavRegion>& regions)
{
    if (w.size() < 12 || std::memcmp (w.data(), "RIFF", 4) != 0 || std::memcmp (w.data() + 8, "WAVE", 4) != 0)
        return {};
    auto put32 = [] (std::vector<std::uint8_t>& o, std::uint32_t v) {
        for (int i = 0; i < 4; ++i)
            o.push_back ((std::uint8_t) (v >> (8 * i)));
    };
    auto tag = [] (std::vector<std::uint8_t>& o, const char* id) { o.insert (o.end(), id, id + 4); };
    // a text entry of the label list: id, the text, a zero, padded to an even size
    auto text = [&] (std::vector<std::uint8_t>& o, const char* id, std::uint32_t cue, const std::string& t) {
        tag (o, id);
        put32 (o, (std::uint32_t) (4 + t.size() + 1));
        put32 (o, cue);
        o.insert (o.end(), t.begin(), t.end());
        o.push_back (0);
        if ((t.size() + 1) & 1)
            o.push_back (0);
    };

    // the new chunks: cue, LIST/adtl, tlst (FL's order)
    std::vector<std::uint8_t> made;
    if (! regions.empty())
    {
        tag (made, "cue ");
        put32 (made, (std::uint32_t) (4 + 24 * regions.size()));
        put32 (made, (std::uint32_t) regions.size());
        for (std::size_t i = 0; i < regions.size(); ++i)
        {
            put32 (made, (std::uint32_t) i + 1);
            put32 (made, (std::uint32_t) regions[i].start);
            tag (made, "data");
            put32 (made, 0);
            put32 (made, 0);
            put32 (made, (std::uint32_t) regions[i].start);
        }
        std::vector<std::uint8_t> adtl;
        tag (adtl, "adtl");
        for (std::size_t i = 0; i < regions.size(); ++i)
        {
            const auto& r = regions[i];
            const auto id = (std::uint32_t) i + 1;
            if (! r.name.empty())
                text (adtl, "labl", id, r.name);
            if (! r.info.empty())
                text (adtl, "note", id, r.info);
            if (! r.marker || ! r.purpose.empty())
            {
                tag (adtl, "ltxt");
                put32 (adtl, 20);
                put32 (adtl, id);
                put32 (adtl, r.marker ? 0u : (std::uint32_t) std::max<std::int64_t> (0, r.end - r.start + 1));
                auto purpose = r.purpose.size() == 4 ? r.purpose : std::string ("rgn ");
                adtl.insert (adtl.end(), purpose.begin(), purpose.end());
                put32 (adtl, 0);
                put32 (adtl, 0);
            }
        }
        if (adtl.size() > 4)
        {
            tag (made, "LIST");
            put32 (made, (std::uint32_t) adtl.size());
            made.insert (made.end(), adtl.begin(), adtl.end());
        }
        std::uint32_t keyed = 0;
        for (const auto& r : regions)
            keyed += r.key >= 0 && r.key <= 127 ? 1u : 0u;
        if (keyed > 0)
        {
            tag (made, "tlst");
            put32 (made, 4 + 24 * keyed);
            put32 (made, keyed);
            for (std::size_t i = 0; i < regions.size(); ++i)
                if (regions[i].key >= 0 && regions[i].key <= 127)
                {
                    tag (made, "cue ");
                    put32 (made, (std::uint32_t) i);
                    put32 (made, 1);
                    made.insert (made.end(), { 0xff, (std::uint8_t) regions[i].key, 0x90, 0 });
                    put32 (made, 0);
                    put32 (made, 0);
                }
        }
    }

    // the file without its old three chunks; the new ones go where the first of them was (else before LIST/INFO,
    // else at the end)
    std::vector<std::uint8_t> out (w.begin(), w.begin() + 12);
    std::size_t at = 0, info = 0;
    for (std::size_t p = 12; p + 8 <= w.size();)
    {
        const std::size_t size = le32 (w.data() + p + 4);
        const std::size_t have = std::min (size, w.size() - (p + 8));
        const auto* body = w.data() + p + 8;
        const std::size_t next = std::min (w.size(), p + 8 + size + (size & 1));
        const bool list = std::memcmp (w.data() + p, "LIST", 4) == 0 && have >= 4;
        const bool old = std::memcmp (w.data() + p, "cue ", 4) == 0 || std::memcmp (w.data() + p, "tlst", 4) == 0 || (list && std::memcmp (body, "adtl", 4) == 0);
        if (old && at == 0)
            at = out.size();
        if (list && std::memcmp (body, "INFO", 4) == 0 && info == 0)
            info = out.size();
        if (std::memcmp (w.data() + p, "strc", 4) == 0 && have >= 28)
        {
            // FL's slice list follows the regions (round 190aj, nine of FL's own saves): its 28-byte header with the
            // count (regions + 1), one entry (2, 0, …) and one (0, start, …) per region, 24 bytes each
            tag (out, "strc");
            put32 (out, (std::uint32_t) (28 + 24 * (regions.size() + 1)));
            put32 (out, le32 (body));
            put32 (out, (std::uint32_t) regions.size() + 1);
            out.insert (out.end(), body + 8, body + 28);
            for (std::size_t i = 0; i <= regions.size(); ++i)
            {
                put32 (out, i == 0 ? 2u : 0u);
                put32 (out, i == 0 ? 0u : (std::uint32_t) regions[i - 1].start);
                for (int k = 0; k < 4; ++k)
                    put32 (out, 0);
            }
        }
        else if (! old)
            out.insert (out.end(), w.begin() + (std::ptrdiff_t) p, w.begin() + (std::ptrdiff_t) next);
        p = next;
    }
    if (at == 0)
        at = info != 0 ? info : out.size();
    out.insert (out.begin() + (std::ptrdiff_t) at, made.begin(), made.end());
    const auto riff = (std::uint32_t) (out.size() - 8);
    for (int i = 0; i < 4; ++i)
        out[4 + (std::size_t) i] = (std::uint8_t) (riff >> (8 * i));
    return out;
}

namespace
{
// offsets in a sampler chunk's body: the loop count, then the first loop's start and end
constexpr std::size_t smplLoops = 28, smplLoopStart = 44, smplLoopEnd = 48, smplOneLoop = 60;
bool isWav (const std::vector<std::uint8_t>& w) noexcept
{
    return w.size() >= 12 && std::memcmp (w.data(), "RIFF", 4) == 0 && std::memcmp (w.data() + 8, "WAVE", 4) == 0;
}
void store32 (std::uint8_t* p, std::uint32_t v) noexcept
{
    for (int i = 0; i < 4; ++i)
        p[i] = (std::uint8_t) (v >> (8 * i));
}
} // namespace

bool wavLoop (const std::vector<std::uint8_t>& w, std::int64_t& first, std::int64_t& last)
{
    if (! isWav (w))
        return false;
    for (std::size_t p = 12; p + 8 <= w.size();)
    {
        const std::size_t size = le32 (w.data() + p + 4);
        const std::size_t have = std::min (size, w.size() - (p + 8));
        if (std::memcmp (w.data() + p, "smpl", 4) == 0 && have >= smplOneLoop && le32 (w.data() + p + 8 + smplLoops) >= 1)
        {
            first = (std::int64_t) le32 (w.data() + p + 8 + smplLoopStart);
            last = (std::int64_t) le32 (w.data() + p + 8 + smplLoopEnd);
            return true;
        }
        p += 8 + size + (size & 1);
    }
    return false;
}

std::vector<std::uint8_t> withLoop (const std::vector<std::uint8_t>& w, std::int64_t first, std::int64_t last)
{
    if (! isWav (w) || first < 0 || last < first)
        return {};
    // a sampler chunk that has a loop: its points are replaced where they are (what FL's own save shows)
    for (std::size_t p = 12; p + 8 <= w.size();)
    {
        const std::size_t size = le32 (w.data() + p + 4);
        const std::size_t have = std::min (size, w.size() - (p + 8));
        if (std::memcmp (w.data() + p, "smpl", 4) == 0 && have >= smplOneLoop && le32 (w.data() + p + 8 + smplLoops) >= 1)
        {
            auto out = w;
            store32 (out.data() + p + 8 + smplLoopStart, (std::uint32_t) first);
            store32 (out.data() + p + 8 + smplLoopEnd, (std::uint32_t) last);
            return out;
        }
        p += 8 + size + (size & 1);
    }
    // none: one is written after the audio (FL's chunk order: fmt, data, smpl…), a loop-less one is dropped
    std::uint32_t rate = 44100;
    std::vector<std::uint8_t> out (w.begin(), w.begin() + 12);
    bool placed = false;
    for (std::size_t p = 12; p + 8 <= w.size();)
    {
        const std::size_t size = le32 (w.data() + p + 4);
        const std::size_t have = std::min (size, w.size() - (p + 8));
        const std::size_t next = std::min (w.size(), p + 8 + size + (size & 1));
        if (std::memcmp (w.data() + p, "fmt ", 4) == 0 && have >= 8)
            rate = le32 (w.data() + p + 12);
        if (std::memcmp (w.data() + p, "smpl", 4) != 0)
            out.insert (out.end(), w.begin() + (std::ptrdiff_t) p, w.begin() + (std::ptrdiff_t) next);
        if (std::memcmp (w.data() + p, "data", 4) == 0 && ! placed)
        {
            placed = true;
            if (out.size() & 1)
                out.push_back (0);
            const auto at = out.size();
            out.resize (at + 8 + smplOneLoop, 0);
            std::memcpy (out.data() + at, "smpl", 4);
            store32 (out.data() + at + 4, (std::uint32_t) smplOneLoop);
            auto* body = out.data() + at + 8;
            store32 (body + 8, rate > 0 ? (std::uint32_t) std::llround (1.0e9 / (double) rate) : 0u); // the sample period, ns
            store32 (body + 12, 60);                 // the unity note
            store32 (body + smplLoops, 1);
            store32 (body + 36, 0x20000u);           // the loop's id as FL writes it
            store32 (body + smplLoopStart, (std::uint32_t) first);
            store32 (body + smplLoopEnd, (std::uint32_t) last);
        }
        p = next;
    }
    if (! placed)
        return {};
    store32 (out.data() + 4, (std::uint32_t) (out.size() - 8));
    return out;
}

std::string regionName (const WavRegion& r, int index)
{
    if (! r.name.empty())
        return r.name;
    return (r.marker ? "Marker #" : "Region #") + std::to_string (index + 1);
}

int autoBaseKey (int regions) noexcept
{
    return regions <= 65 ? 60 : std::max (0, 60 - 12 * ((regions - 54) / 12));
}

KeyMap keyMap (int deck, const std::vector<WavRegion>& deckA, const std::vector<WavRegion>& deckB, bool sameDeckOnly)
{
    KeyMap map;
    const std::vector<WavRegion>* decks[2] = { &deckA, &deckB };
    // regions with a key of their own, of both decks: they layer on the same note
    for (int s = 0; s < 2; ++s)
    {
        if (sameDeckOnly && s != deck)
            continue;
        for (std::size_t i = 0; i < decks[s]->size(); ++i)
            if (const int key = (*decks[s])[i].key; key >= 0 && key <= 127)
                map[(std::size_t) key].push_back ((std::int32_t) ((s << 16) | (int) i));
    }
    // the others take base + index on their own deck, where no keyed region sits
    const auto& own = *decks[deck != 0 ? 1 : 0];
    const int base = autoBaseKey ((int) own.size());
    for (std::size_t i = 0; i < own.size(); ++i)
    {
        const int key = base + (int) i;
        if (own[i].key == -1 && key >= 0 && key <= 127 && map[(std::size_t) key].empty())
            map[(std::size_t) key].push_back ((std::int32_t) (((deck != 0 ? 1 : 0) << 16) | (int) i));
    }
    return map;
}
} // namespace lynx::slicex
