#include "core/FlmDwSampler.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>

#include "core/Instruments.h"

// FL Studio Mobile's DW Sampler (round 190de; FLM_FORMAT §7.5). Clean-room: the laws below are the behaviour read in
// FLM's FL plugin, written in LYNX's own words and code.

namespace lynx::flmdw
{
const float defaults[paramCount] = { 1.0f, 0.0f, 0.1f, 0.5f, 0.5f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                     0.0f, 1.0f, 1.0f, 1.0f, 0.5f, 0.0f, 1.0f, 1.0f, 1.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f, 0.0f, 0.0f };

const char* paramName (int k) noexcept
{
    static const char* const names[paramCount] = { "Enable", "Attack", "Release", "Pitch", "Fine Tune", "Decay", "Sustain", "Slide", "Reverse",
                                                   "Loop", "Legato", "Mono", "Cutoff", "Resonance", "Kbd. Track", "Vel. Track", "Filter type",
                                                   "Filter Attack", "Filter Decay", "Filter Sustain", "Filter Release", "Filter Env. Amount",
                                                   "Pitch Attack", "Pitch Decay", "Pitch Sustain", "Pitch Release", "Pitch Env. Amount", "LFO Shape",
                                                   "LFO Rate", "LFO Mode", "LFO Sync Rate", "LFO Amount", "LFO Destination", "LFO Attack" };
    return k >= 0 && k < paramCount ? names[k] : "";
}

// ------------------------------------------------------------------------------------------------- saved state
namespace
{
struct Reader
{
    std::span<const std::uint8_t> b;
    std::size_t at = 0;
    bool fits (std::size_t n) const noexcept { return at + n <= b.size(); }
    std::uint32_t u32 (std::size_t p) const noexcept
    {
        std::uint32_t v = 0;
        for (int k = 0; k < 4; ++k)
            v |= (std::uint32_t) b[p + (std::size_t) k] << (8 * k);
        return v;
    }
    std::uint64_t u64 (std::size_t p) const noexcept { return (std::uint64_t) u32 (p) | ((std::uint64_t) u32 (p + 4) << 32); }
    float f32 (std::size_t p) const noexcept
    {
        const auto v = u32 (p);
        float f;
        std::memcpy (&f, &v, 4);
        return f;
    }
};

// a saved path: the root marker, a NUL, the relative path up to the next NUL (later bytes are stale text, ignored)
void splitPath (const std::uint8_t* p, std::size_t n, std::string& root, std::string& path)
{
    std::string text ((const char*) p, n);
    root.clear();
    path.clear();
    if (text.size() >= 4 && text[0] == '@' && text[2] == '@' && text[3] == '\0')
    {
        root = text.substr (0, 3);
        text = text.substr (4);
    }
    if (const auto zero = text.find ('\0'); zero != std::string::npos)
        text.resize (zero);
    std::replace (text.begin(), text.end(), '\\', '/');
    path = text;
}
} // namespace

std::optional<Custom> parseCustom (std::span<const std::uint8_t> bytes)
{
    if (bytes.size() < 4 || std::memcmp (bytes.data(), "10WD", 4) != 0)
        return std::nullopt;
    Custom out;
    Reader r { bytes, 4 };
    while (r.fits (8))
    {
        const auto* tag = bytes.data() + r.at;
        const auto size = (std::size_t) r.u32 (r.at + 4);
        const auto body = r.at + 8;
        if (body + size > bytes.size())
            break;
        if (std::memcmp (tag, "ZONE", 4) == 0 && size >= 1065 && ! out.hasPath)
        {
            ZoneRecord z;
            splitPath (bytes.data() + body, 1024, z.root, z.path);
            const auto t = body + 1024;
            z.rootKey = bytes[t];
            z.keyLow = bytes[t + 1];
            z.keyHigh = bytes[t + 2];
            z.velLow = bytes[t + 3];
            z.velHigh = bytes[t + 4];
            z.gain = r.f32 (t + 5);
            z.pan = r.f32 (t + 9);
            z.loopMode = (int) r.u32 (t + 13);
            z.loopStart = (std::int32_t) r.u32 (t + 17);
            z.loopEnd = (std::int32_t) r.u32 (t + 21);
            z.attack = r.f32 (t + 25);
            z.decay = r.f32 (t + 29);
            z.sustain = r.f32 (t + 33);
            z.release = r.f32 (t + 37);
            out.zones.push_back (std::move (z));
        }
        else if ((std::memcmp (tag, "PATh", 4) == 0 || std::memcmp (tag, "PATH", 4) == 0) && size >= 4 && out.zones.empty() && ! out.hasPath)
        {
            const auto n = (std::size_t) r.u32 (body);
            if (4 + n <= size)
            {
                out.hasPath = true;
                splitPath (bytes.data() + body + 4, n, out.pathRoot, out.path);
                if (tag[3] == 'h' && 4 + n + 8 <= size)
                {
                    out.pathFloats = true;
                    out.pathAttack = r.f32 (body + 4 + n);
                    out.pathRelease = r.f32 (body + 8 + n);
                }
            }
        }
        else if (std::memcmp (tag, "MIDI", 4) == 0 && size >= 2)
        {
            out.pbRange = bytes[body];
            out.smoothAttack = bytes[body + 1] != 0;
        }
        r.at = body + size;
    }
    return out;
}

std::vector<ZoneRecord> parseProgram (std::span<const std::uint8_t> bytes)
{
    std::vector<ZoneRecord> out;
    if (bytes.size() < 8 || std::memcmp (bytes.data(), "DwPr", 4) != 0)
        return out;
    const Reader r { bytes, 0 };
    if (r.u32 (4) <= 35)
        return out;
    // entries: u32 id, u64 size, body
    const auto walk = [&] (std::size_t from, std::size_t to, auto&& each) {
        auto p = from;
        while (p + 12 <= to)
        {
            const auto id = r.u32 (p);
            const auto size = r.u64 (p + 4);
            if (size > to - p - 12)
                return;
            if (! each (id, p + 12, (std::size_t) size))
                return;
            p += 12 + (std::size_t) size;
        }
    };
    walk (8, bytes.size(), [&] (std::uint32_t id, std::size_t p, std::size_t n) {
        if (id != 1)
            return true;
        walk (p, p + n, [&] (std::uint32_t sub, std::size_t q, std::size_t m) {
            if (sub == 2)
                return false; // the program's end
            if (sub != 3)
                return true;
            ZoneRecord z;
            z.keyHigh = 127;
            bool ok = true;
            walk (q, q + m, [&] (std::uint32_t k, std::size_t s, std::size_t len) {
                if (k == 500 && len >= 17)
                {
                    z.rootKey = bytes[s], z.keyLow = bytes[s + 1], z.keyHigh = bytes[s + 2], z.velLow = bytes[s + 3], z.velHigh = bytes[s + 4];
                    z.gain = r.f32 (s + 9);
                    z.pan = r.f32 (s + 13);
                }
                else if (k == 502)
                    splitPath (bytes.data() + s, len, z.root, z.path);
                else if (k == 503 && len >= 32)
                {
                    const auto channels = r.u32 (s + 8);
                    ok = channels == 1 || channels == 2;
                    z.loopMode = (int) r.u32 (s + 20);
                    z.loopStart = r.u32 (s + 24);
                    z.loopEnd = r.u32 (s + 28);
                }
                else if (k == 509 && len >= 16)
                {
                    z.attack = r.f32 (s), z.decay = r.f32 (s + 4), z.sustain = r.f32 (s + 8), z.release = r.f32 (s + 12);
                }
                return true;
            });
            if (ok && ! z.path.empty())
                out.push_back (std::move (z));
            return true;
        });
        return false;
    });
    return out;
}

// ------------------------------------------------------------------------------------------------- the channel
bool isChannel (const ChannelDef& c) noexcept
{
    return c.instrument == InstrumentKind::sampler && c.modelParams.size() >= (std::size_t) (head + paramCount) && c.modelParams[0] == marker;
}

float value (const ChannelDef& c, int k) noexcept
{
    if (k < 0 || k >= paramCount)
        return 0.0f;
    if (! isChannel (c))
        return defaults[k];
    const auto v = c.modelParams[(std::size_t) (head + k)];
    return std::isfinite (v) ? std::clamp (v, 0.0f, 1.0f) : defaults[k];
}

std::vector<float> channelParams (const std::vector<float>& values, int pbRange, bool smoothAttack, const std::vector<bool>& zoneDecays)
{
    std::vector<float> out { marker, (float) std::clamp (pbRange, 0, 48), smoothAttack ? 1.0f : 0.0f };
    for (int k = 0; k < paramCount; ++k)
        out.push_back (k < (int) values.size() && std::isfinite (values[(std::size_t) k]) ? std::clamp (values[(std::size_t) k], 0.0f, 1.0f) : defaults[k]);
    for (const auto d : zoneDecays)
        out.push_back (d ? 1.0f : 0.0f);
    return out;
}

SampleZone sampleZone (const ZoneRecord& z, const std::string& resolvedPath)
{
    SampleZone s;
    s.path = resolvedPath;
    s.rootKey = std::clamp (z.rootKey, 0, 127);
    s.lowKey = z.keyLow;
    s.highKey = z.keyHigh;
    s.lowVelocity = z.velLow;
    s.highVelocity = z.velHigh;
    s.gain = std::isfinite (z.gain) ? z.gain : 1.0f; // (FLM applies it above 0.0001 only, and only with Loop on)
    s.loopMode = z.loopMode >= 2 ? 1 : 0;
    s.loopStart = std::max<std::int64_t> (0, z.loopStart);
    s.loopEnd = std::max<std::int64_t> (0, z.loopEnd);
    return s;
}

// ------------------------------------------------------------------------------------------------- laws
double envelopeSeconds (float v) noexcept
{
    const auto x = (double) std::clamp (v, 0.0f, 1.0f);
    return 10.0 * x * x * x * x + 0.001;
}

double onePoleRate (float v, double k) noexcept
{
    if (v < 0.001f)
        return 1.0;
    const auto a = 0.95f * v + 0.05f;
    return 0.0002 / ((double) (a * a * a * a) * k);
}

int pitchSemitones (float v) noexcept { return (int) (v * 48.0f - 24.0f); }
int filterKind (float v) noexcept { return std::clamp ((int) (v * 3.0f), 0, 3); }

double cutoffHz (float cutoff, double keyTerm, double envTerm) noexcept
{
    return std::min (std::exp2 (keyTerm + 10.4 * (double) std::clamp (cutoff, 0.0f, 1.0f) + 4.0 + envTerm), 22000.0);
}

double lfoHz (float v) noexcept
{
    const auto x = 1.0 - (double) std::clamp (v, 0.0f, 1.0f);
    return 1.0 / (x * x * x * 30.0 + 1.0 / 30.0);
}

// ------------------------------------------------------------------------------------------------- the engine
namespace
{
constexpr int voiceCount = 128;
constexpr int block = 512; // the render call FLM plans its glides and global tune for (FL's rack hands it 512 samples)

// FLM's 4-pole ladder low-pass: four one-pole stages with the output fed back, as 4096 explicit steps per sample with
// the input held; the step's 4×4 transition is squared 12 times into the sample's (y ← A·y + B·x)
struct Ladder
{
    // in float and in FLM's order of operations (measured against FLM inside FL: the ladder nulls at −73 dB only so)
    float m[16] {}, b[4] {};   // column-major M[r + 4c]
    float y[2][4] {};

    void design (double fcD, double fs, double kD) noexcept
    {
        const auto fc = (float) fcD;
        auto g = (float) ((0.000244140625 / fs) * (double) (fc * 6.25f));
        g = std::clamp (g, 0.0f, 1.0f);
        const auto k = std::clamp ((float) kD, 0.0f, 1.0f);
        const float h = 1.0f - g;
        float a = k * -4.0f * g;
        for (auto& v : m) v = 0.0f;
        const float hg = h * g;
        m[0] = h; m[12] = a;
        a = a * g; m[13] = a;
        a = a * g; m[14] = a;
        const float hgg = hg * g;
        m[1] = hg; m[5] = h; m[15] = a * g + h; m[2] = hgg; m[6] = hg; m[10] = h;
        m[3] = hgg * g; m[7] = hgg; m[11] = hg;
        b[0] = g; b[1] = g * g; b[2] = g * g * g; b[3] = g * g * g * g;
        for (int r = 0; r < 12; ++r)
        {
            float n[16], nb[4];
            for (int i = 0; i < 4; ++i)
            {
                float acc = b[i];
                for (int j = 0; j < 4; ++j) acc += m[i + 4 * j] * b[j];
                nb[i] = acc;
            }
            for (int rr = 0; rr < 4; ++rr)
                for (int c = 0; c < 4; ++c)
                {
                    float acc = 0.0f;
                    for (int j = 0; j < 4; ++j) acc += m[rr + 4 * j] * m[j + 4 * c];
                    n[rr + 4 * c] = acc;
                }
            std::memcpy (m, n, sizeof (m));
            std::memcpy (b, nb, sizeof (b));
        }
    }

    double run (int ch, double xD) noexcept
    {
        const auto x = (float) xD;
        float n[4];
        auto& st = y[ch];
        for (int i = 0; i < 4; ++i)
            n[i] = m[i] * st[0] + m[i + 4] * st[1] + m[i + 8] * st[2] + m[i + 12] * st[3] + b[i] * x;
        std::memcpy (st, n, sizeof (n));
        return st[3];
    }
};

// FLM's band-pass (RBJ constant skirt) and its high-pass (RBJ poles with Q / 2, numerator as RBJ's with Q)
struct Biquad
{
    double b0 = 0, b2 = 0, a1 = 0, a2 = 0, bb1 = 0; // y = b0·x + bb1·x1 + b2·x2 − a1·y1 − a2·y2
    double x1[2] {}, x2[2] {}, y1[2] {}, y2[2] {};
    double lastFc = -1, lastQ = -1, lastFs = -1;
    int lastKind = 0;

    static double flush (double v) noexcept { return std::abs (v) < 1.0e-9 ? 0.0 : v; }

    void design (int kind, double fc, double q, double fs) noexcept
    {
        if (fc == lastFc && q == lastQ && fs == lastFs && kind == lastKind)
            return;
        lastFc = fc, lastQ = q, lastFs = fs, lastKind = kind;
        const auto w = 2.0 * 3.14159265358979323846 * fc / fs, sn = std::sin (w), cs = std::cos (w);
        if (kind == 2)
        {
            const auto alpha = sn / (2.0 * q), n = 1.0 / (1.0 + alpha);
            b0 = flush (n * sn * 0.5), bb1 = 0.0, b2 = flush (-n * sn * 0.5);
            a1 = flush (-n * 2.0 * cs), a2 = flush (n * (1.0 - alpha));
        }
        else
        {
            b0 = flush ((1.0 + cs) / (sn / q + 2.0));
            bb1 = -2.0 * b0, b2 = b0;
            a1 = flush (-2.0 * q * cs / (sn + q));
            a2 = flush ((q - sn) / (q + sn));
        }
    }

    double run (int ch, double x) noexcept
    {
        const auto y = flush (b0 * x + bb1 * x1[ch] + b2 * x2[ch] - a1 * y1[ch] - a2 * y2[ch]);
        x2[ch] = x1[ch], x1[ch] = x;
        y2[ch] = y1[ch], y1[ch] = y;
        return y;
    }
};

struct Voice
{
    bool active = false;
    int key = 60, zone = -1;
    double pos = 0.0, baseRate = 1.0;
    bool looping = false, backwards = false;
    double loopStart = 0.0, loopEnd = 0.0;
    float velocity = 0.0f;               // the note's velocity × the zone's gain (Loop on)
    float panL = 1.0f, panR = 1.0f;
    float pitchOffset = 0.0f;            // the note's own pitch (semitones)
    int state = 0;                       // 0 attack, 1 decay, 2 release
    double env = 0.0, atk = 1.0, dec = 1.0, rel = 1.0, sus = 1.0;
    double fenv = 0.0, ftgt = 4.0, frate = 1.0;
    double penv = 0.0, ptgt = 4.0, prate = 1.0;
    double glide = 0.0, glideTarget = 0.0, glideStep = 0.0;
    double lfoPhase = 0.0, lfoFade = 0.0, lfo = 0.0, lfoPitch = 1.0, held = 0.0;
    double gL = 1.0, gR = 1.0;
    bool firstSample = true;
    int counter = 0;
    Ladder ladder;
    Biquad biquad;
};
} // namespace

class FlmDwSamplerInstrument final : public Instrument
{
public:
    explicit FlmDwSamplerInstrument (std::vector<EngineZone> z) : zones (std::move (z))
    {
        for (int k = 0; k < paramCount; ++k)
            values[(std::size_t) k].store (defaults[k]);
        lastPick.fill (-1);
    }

    void prepare (double sampleRate) noexcept override
    {
        fs = sampleRate > 0.0 ? sampleRate : 44100.0;
        reset();
    }

    void configure (const ChannelDef& c, double bpm) noexcept override
    {
        tempo.store (bpm > 0.0 ? (float) bpm : 120.0f);
        knobCents.store (c.sampler.pitch * 100.0f);
        if (! isChannel (c))
            return;
        for (int k = 0; k < paramCount; ++k)
            values[(std::size_t) k].store (value (c, k));
        pbRange.store ((int) c.modelParams[1]);
        smooth.store (c.modelParams[2] != 0.0f);
        for (std::size_t z = 0; z < zones.size(); ++z)
        {
            const auto at = (std::size_t) (head + paramCount) + z;
            zones[z].decay = at >= c.modelParams.size() || c.modelParams[at] != 0.0f;
        }
    }

    void setParameter (int index, float v) noexcept override
    {
        const auto k = index - automationBase;
        if (k >= 0 && k < paramCount && std::isfinite (v))
            values[(std::size_t) k].store (std::clamp (v, 0.0f, 1.0f));
    }

    void setChannelPitchNow (float knob, float rest) noexcept override { liveCents = knob + rest; }
    void setNextNoteExpression (float pan, float cents, bool) noexcept override
    {
        nextPan = pan;
        nextCents = cents;
    }
    void setSongPosition (double ticks) noexcept override
    {
        playing = lastTicks >= 0.0 && ticks != lastTicks;
        lastTicks = ticks;
        songBeats = ticks / (double) ppq;
    }

    void noteOn (int key, float velocity) noexcept override
    {
        const auto pan = std::clamp (0.5f + 0.5f * nextPan, 0.0f, 1.0f);
        const auto cents = nextCents;
        nextPan = 0.0f, nextCents = 0.0f;
        key = std::clamp (key, 0, 127);
        heldVelocity[(std::size_t) key] = std::clamp (velocity, 0.0f, 1.0f);
        pressTime[(std::size_t) key] = ++clock;
        if (P (param::enable) < 0.5f)
            return;
        if (P (param::mono) != 0.0f)
            monoNoteOn (key, velocity, pan, cents);
        else
        {
            for (auto& v : voices)
                if (! v.active)
                {
                    start (v, key, velocity, pan, cents, glideFrom (key), false);
                    break;
                }
        }
        lastKey = key;
    }

    void noteOff (int key) noexcept override
    {
        key = std::clamp (key, 0, 127);
        heldVelocity[(std::size_t) key] = 0.0f;
        pressTime[(std::size_t) key] = 0;
        if (P (param::mono) != 0.0f && mono >= 0 && voices[(std::size_t) mono].active && voices[(std::size_t) mono].key == key)
        {
            // last-note priority: glide back to the most recent key still held, else release
            int best = -1;
            std::uint64_t when = 0;
            for (int k = 0; k < 128; ++k)
                if (pressTime[(std::size_t) k] > when)
                    when = pressTime[(std::size_t) k], best = k;
            auto& v = voices[(std::size_t) mono];
            if (best >= 0)
                v.glideTarget = best - v.key;
            else
                release (v, false);
            return;
        }
        for (auto& v : voices)
            if (v.active && v.key == key && v.state != 2)
                release (v, false);
    }

    void allNotesOff() noexcept override
    {
        for (auto& v : voices)
            if (v.active)
                release (v, false);
        heldVelocity.fill (0.0f);
        pressTime.fill (0);
    }
    void cut() noexcept override
    {
        for (auto& v : voices)
            if (v.active)
                release (v, true);
    }
    void panic() noexcept override { reset(); }
    bool idle() const noexcept override
    {
        return std::none_of (voices.begin(), voices.end(), [] (const Voice& v) { return v.active; });
    }

    void render (float* left, float* right, int n, float gainLeft, float gainRight) noexcept override
    {
        if (P (param::enable) < 0.5f)
            return;
        for (int pos = 0; pos < n; pos += block)
        {
            const auto c = std::min (block, n - pos);
            renderCall (left + pos, right + pos, c, gainLeft, gainRight, playing ? songBeats + (double) pos * tempo.load() / (60.0 * fs) : -1.0);
        }
    }

private:
    float P (int k) const noexcept { return values[(std::size_t) k].load (std::memory_order_relaxed); }

    void reset() noexcept
    {
        for (auto& v : voices)
            v = Voice {};
        lastPick.fill (-1);
        heldVelocity.fill (0.0f);
        pressTime.fill (0);
        mono = 0;
        lastKey = 0;
        rng = 0;
    }

    double random() noexcept
    {
        const auto r = (double) rng / 4294967296.0;
        rng = rng * 0x4D66B561u + 0x16C0A8E8u;
        return r;
    }

    // the glide a new note starts from: from the previous note's key; with Legato only while another key is held
    double glideFrom (int key) const noexcept
    {
        if (P (param::legato) != 0.0f)
        {
            bool other = false;
            for (int k = 0; k < 128; ++k)
                other = other || (k != key && pressTime[(std::size_t) k] != 0);
            if (! other)
                return 0.0;
        }
        return (double) (lastKey - key);
    }

    // the zone a note plays (FLM's choice: the key moved by the Pitch knob, several matches drawn, never the same twice in a row per key)
    int zoneFor (int key, float velocity) noexcept
    {
        const auto kz = key + pitchSemitones (P (param::pitch));
        const auto vz = (int) (velocity * 127.0f + 0.5f);
        int match[voiceCount];
        int count = 0;
        for (int i = 0; i < (int) zones.size() && count < voiceCount; ++i)
        {
            const auto& z = zones[(std::size_t) i].zone;
            if (z.lowKey <= kz && kz <= z.highKey && z.lowVelocity <= vz && vz <= z.highVelocity)
                match[count++] = i;
        }
        if (count == 0)
            return -1;
        if (count == 1)
            return match[0];
        int i = 0;
        for (int guard = 0; guard < 64; ++guard)
        {
            i = (int) std::lround (std::min (random(), 1.0) * (double) (count - 1));
            if (i != lastPick[(std::size_t) key])
                break;
        }
        lastPick[(std::size_t) key] = i;
        return match[i];
    }

    void start (Voice& v, int key, float velocity, float pan, float cents, double glideOffset, bool fromRelease) noexcept
    {
        const auto zi = zoneFor (key, velocity);
        if (zi < 0)
            return;
        const auto& z = zones[(std::size_t) zi];
        const auto wasActive = v.active;
        if (! wasActive)
            v = Voice {};
        v.active = true;
        v.key = key;
        v.zone = zi;
        v.pitchOffset = cents * 0.01f;
        const auto fileRate = z.sample != nullptr ? z.sample->sampleRate : 44100.0;
        v.baseRate = std::exp2 ((double) (key - z.zone.rootKey) / 12.0) * fileRate / fs;
        // amp envelope (times fixed now)
        const auto attack = P (param::attack);
        v.atk = (attack * attack * attack * attack == 0.0f && ! smooth.load()) ? 1.0 : 1.0 / (envelopeSeconds (attack) * fs);
        v.dec = z.decay ? 1.0 / (envelopeSeconds (P (param::decay)) * fs) : 1.0;
        v.rel = 1.0 / (envelopeSeconds (P (param::release)) * fs);
        v.sus = P (param::sustain);
        v.state = 0;
        if (! fromRelease && ! wasActive)
            v.env = 0.0;
        // loop and the zone's gain, only with Loop on
        auto gain = 1.0f;
        v.looping = false;
        if (P (param::loop) != 0.0f)
        {
            if (z.zone.gain > 0.0001f)
                gain = z.zone.gain;
            auto ls = (double) z.zone.loopStart, le = (double) z.zone.loopEnd;
            if (ls == le && z.sample != nullptr && z.sample->loopStart >= 0 && z.sample->loopEnd > z.sample->loopStart)
                ls = (double) z.sample->loopStart, le = (double) z.sample->loopEnd;
            v.looping = z.zone.loopMode != 0 && ls != le && le > ls;
            v.loopStart = ls, v.loopEnd = le;
        }
        v.velocity = velocity * gain;
        const auto frames = z.sample != nullptr ? (double) z.sample->left.size() : 0.0;
        v.backwards = P (param::reverse) != 0.0f;
        if (v.backwards)
            v.looping = false;
        v.pos = v.backwards ? frames - 1.0 : 0.0;
        // pan: a balance of the note's pan
        const auto d = 2.0f * pan - 1.0f;
        v.panL = d > 0.0f ? 1.0f - d : 1.0f;
        v.panR = d < 0.0f ? 1.0f + d : 1.0f;
        // the envelopes start their attack from where they are
        v.ftgt = 4.0, v.frate = (float) onePoleRate (P (param::filterAttack), 500.0);
        v.ptgt = 4.0, v.prate = onePoleRate (P (param::pitchAttack), 500.0);
        v.lfoPhase = 0.0, v.lfoFade = 0.0;
        v.glide = glideOffset, v.glideTarget = 0.0;
        v.counter = 0;
        v.firstSample = true;
    }

    void release (Voice& v, bool fast) noexcept
    {
        v.state = 2;
        v.ftgt = 0.0, v.frate = (float) onePoleRate (P (param::filterRelease), 50.0);
        v.ptgt = 0.0, v.prate = onePoleRate (P (param::pitchRelease), 50.0);
        if (fast)
            v.rel = 1.0 / (0.001 * fs);
    }

    void monoNoteOn (int key, float velocity, float pan, float cents) noexcept
    {
        auto& m = voices[(std::size_t) mono];
        if (! m.active)
        {
            start (m, key, velocity, pan, cents, glideFrom (key), false);
            return;
        }
        if (m.state != 2)
        {
            m.glideTarget = key - m.key; // no retrigger: the voice glides
            return;
        }
        const auto oldKey = m.key;
        for (auto& v : voices)
            if (v.active)
                release (v, true);
        for (int i = voiceCount - 1; i >= 0; --i)
            if (! voices[(std::size_t) i].active)
            {
                mono = i;
                break;
            }
        start (voices[(std::size_t) mono], key, velocity, pan, cents, (double) (oldKey - key), false);
    }

    void renderCall (float* left, float* right, int n, float gainLeft, float gainRight, double songPos) noexcept
    {
        // per call: the LFO's step, the global tune, the glides' plan
        const auto bpm = (double) tempo.load();
        static constexpr double syncBeats[] = { 0.125, 0.25, 0.5, 0.75, 1.0, 2.0, 4.0, 8.0, 16.0, 32.0 };
        const auto lfoMode = std::clamp ((int) (P (param::lfoMode) * 2.0f), 0, 2);
        const auto beats = syncBeats[std::clamp ((int) (P (param::lfoSync) * 9.0f), 0, 9)];
        const auto period = lfoMode == 2 ? beats * 60.0 / std::max (1.0, bpm) : 1.0 / lfoHz (P (param::lfoRate));
        const auto dPhase = 1.0 / (period * fs);
        const auto channelSemis = (std::isnan (liveCents) ? knobCents.load() : liveCents) * 0.01;
        const auto tune = (double) std::pow (2.0f, ((float) (2.0f * P (param::fineTune)) + (float) pitchSemitones (P (param::pitch)) - 1.0f + (float) channelSemis) / 12.0f);
        const auto slide = P (param::slide) * 0.5f;
        const auto glideTime = (double) (slide * slide * slide);
        const auto pitchAmount = std::pow (2.0 * (double) P (param::pitchAmount) - 1.0, 3.0);
        const auto shape = std::clamp ((int) (P (param::lfoShape) * 3.0f + 0.5f), 0, 3);
        const auto amountRaw = 2.0 * (double) P (param::lfoAmount) - 1.0;
        const auto amount = amountRaw < 0.0 ? -amountRaw * amountRaw : amountRaw * amountRaw;
        const auto dest = std::clamp ((int) (P (param::lfoDest) * 3.0f), 0, 3);
        const auto fade = 16.0 * onePoleRate (P (param::lfoAttack), 500.0);
        const auto kind = filterKind (P (param::filterType));
        const auto res = (double) P (param::resonance);
        const auto q = 16.0 * res * res * res + 0.5;
        const auto cutoff = P (param::cutoff), kbd = P (param::kbdTrack), velTrack = P (param::velTrack);
        const auto fSus = (double) P (param::filterSustain), fDec = onePoleRate (P (param::filterDecay), 50.0);
        const auto pSus = (double) P (param::pitchSustain), pDec = onePoleRate (P (param::pitchDecay), 50.0);

        for (auto& v : voices)
        {
            if (! v.active)
                continue;
            const auto& z = zones[(std::size_t) v.zone];
            if (z.sample == nullptr || z.sample->left.empty())
                continue; // (FLM keeps such a voice: no sound)
            const auto& L = z.sample->left;
            const auto& R = z.sample->right.empty() ? z.sample->left : z.sample->right;
            const auto stereo = ! z.sample->right.empty();
            const auto len = (std::int64_t) L.size();
            v.glideStep = glideTime > 1.0e-5 ? (v.glideTarget - v.glide) / (glideTime * fs) : 0.0;
            if (lfoMode == 2 && songPos >= 0.0)
                v.lfoPhase = songPos / beats - std::floor (songPos / beats);
            const auto w = (double) std::clamp (v.velocity, 0.0f, 1.0f);
            const auto rate = v.baseRate * tune * (v.backwards ? -1.0 : 1.0);
            for (int i = 0; i < n; ++i)
            {
                if (v.counter == 0)
                {
                    updateLfo (v, shape, amount, fade, dPhase, lfoMode, dest);
                    if (kind != 0)
                    {
                        // (in float, in FLM's order: the cutoff law of FLM_FORMAT §7.5)
                        const auto c = std::clamp ((cutoff + (dest == 0 ? (float) v.lfo : 0.0f)) + velTrack * v.velocity, 0.0f, 1.0f);
                        const float envT = ((P (param::filterAmount) * 2.0f - 1.0f) * (float) v.fenv) * 8.0f;
                        const float base = (float) ((double) c * 10.4 + 4.0) + envT;
                        const float kt = (((float) v.key + (float) v.glide) - 48.0f) * kbd * 0.083333336f + base;
                        const auto fc = (double) std::min (std::pow (2.0f, kt), 22000.0f);
                        if (kind == 1)
                            v.ladder.design (fc, fs, std::clamp (res, 0.0, 1.0));
                        else
                            v.biquad.design (kind, fc, q, fs);
                    }
                }
                v.counter = (v.counter + 1) & 15;
                // glide
                if (v.glide != v.glideTarget)
                {
                    const auto next = v.glide + v.glideStep;
                    v.glide = (v.glideStep == 0.0 || (v.glideStep > 0.0 ? next >= v.glideTarget : next <= v.glideTarget)) ? v.glideTarget : next;
                }
                auto pf = (double) std::pow (2.0f, (float) ((double) v.pitchOffset + v.glide) / 12.0f);
                // pitch envelope
                v.penv += (v.ptgt - v.penv) * v.prate;
                if (v.penv > 1.0)
                    v.penv = 1.0, v.ptgt = pSus, v.prate = pDec;
                if (pitchAmount != 0.0)
                    pf *= std::exp2 (4.0 * pitchAmount * v.penv * v.penv * v.penv);
                // read
                const auto idx = (std::int64_t) std::floor (v.pos);
                const auto f = v.pos - (double) idx;
                auto j = idx + 1;
                if (v.looping && (double) j >= v.loopEnd)
                    j = (std::int64_t) v.loopStart;
                const auto at = [len] (const std::vector<float>& b, std::int64_t k) { return k >= 0 && k < len ? (double) b[(std::size_t) k] : 0.0; };
                auto xl = at (L, idx) * (1.0 - f) + at (L, j) * f;
                auto xr = stereo ? at (R, idx) * (1.0 - f) + at (R, j) * f : xl;
                // advance
                v.pos += v.lfoPitch * pf * rate;
                if (v.looping && v.pos >= v.loopEnd)
                    v.pos -= v.loopEnd - v.loopStart;
                // filter envelope, filter
                v.fenv = (double) (((float) v.ftgt - (float) v.fenv) * (float) v.frate + (float) v.fenv);
                if (v.fenv > 1.0)
                    v.fenv = 1.0, v.ftgt = (float) fSus, v.frate = (float) fDec;
                if (kind == 1)
                {
                    xl = v.ladder.run (0, xl);
                    xr = stereo ? v.ladder.run (1, xr) : xl;
                }
                else if (kind != 0)
                {
                    xl = v.biquad.run (0, xl);
                    xr = v.biquad.run (1, xr);
                }
                // amp envelope (linear, heard squared)
                if (v.state == 0)
                {
                    v.env += v.atk;
                    if (v.env >= 1.0)
                        v.env = 1.0, v.state = 1;
                }
                else if (v.state == 1)
                {
                    v.env -= v.dec;
                    if (v.env < v.sus)
                        v.env = v.sus;
                }
                else
                {
                    v.env -= v.rel;
                    if (v.env < 0.0)
                        v.env = 0.0;
                }
                // the LFO's volume / pan gains, smoothed
                double tl = 1.0, tr = 1.0;
                if (dest == 2)
                    tl = tr = 1.0 + v.lfo;
                else if (dest == 3)
                    tl = std::min (1.0, 1.0 + v.lfo), tr = std::min (1.0, 1.0 - v.lfo);
                if (v.firstSample)
                    v.gL = tl, v.gR = tr, v.firstSample = false;
                else
                    v.gL += (tl - v.gL) * 0.01, v.gR += (tr - v.gR) * 0.01;
                const auto e2 = v.env * v.env * w;
                left[i] += (float) (v.gL * xl * e2 * v.panL) * gainLeft;
                right[i] += (float) (v.gR * xr * e2 * v.panR) * gainRight;
            }
            // the voice ends past its sample, or silent after its attack
            const auto ip = (std::int64_t) std::floor (v.pos);
            if ((! v.backwards && ip > len) || (v.backwards && ip < 0) || (v.state != 0 && v.env <= 0.0))
                v.active = false;
        }
    }

    void updateLfo (Voice& v, int shape, double amount, double fade, double dPhase, int mode, int dest) noexcept
    {
        constexpr double twoPi = 6.283185307179586;
        double raw;
        switch (shape)
        {
            case 0: raw = std::sin (twoPi * v.lfoPhase); break;
            case 1: raw = 1.0 - 2.0 * v.lfoPhase; break;
            case 2: raw = v.lfoPhase >= 0.5 ? 1.0 : -1.0; break;
            default: raw = v.held; break;
        }
        v.lfoFade = std::min (1.0, v.lfoFade + fade);
        v.lfo = raw * amount * v.lfoFade;
        v.lfoPhase += 16.0 * dPhase;
        if (v.lfoPhase >= 1.0)
        {
            if (mode == 1)
                v.lfoPhase = 1.0;
            else
            {
                v.lfoPhase -= 1.0;
                if (shape == 3)
                {
                    v.held = random();
                    v.lfo = v.held * amount;
                }
            }
        }
        v.lfoPitch = dest == 1 ? std::exp2 ((v.lfo < 0.0 ? -1.0 : 1.0) * v.lfo * v.lfo) : 1.0;
    }

    std::vector<EngineZone> zones;
    std::array<std::atomic<float>, paramCount> values {};
    std::atomic<float> tempo { 120.0f }, knobCents { 0.0f };
    std::atomic<int> pbRange { 4 };
    std::atomic<bool> smooth { true };
    double fs = 44100.0;
    std::array<Voice, voiceCount> voices {};
    std::array<int, 128> lastPick {};
    std::array<float, 128> heldVelocity {};
    std::array<std::uint64_t, 128> pressTime {};
    std::uint64_t clock = 0;
    int mono = 0, lastKey = 0;
    std::uint32_t rng = 0;
    float nextPan = 0.0f, nextCents = 0.0f;
    float liveCents = std::numeric_limits<float>::quiet_NaN();
    double lastTicks = -1.0, songBeats = 0.0;
    bool playing = false;
};

std::unique_ptr<Instrument> makeInstrument (std::vector<EngineZone> zones) { return std::make_unique<FlmDwSamplerInstrument> (std::move (zones)); }
} // namespace lynx::flmdw
