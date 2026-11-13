#include "core/WaveTraveller.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

#include "core/Denormals.h"
#include "core/FlVoiceProcessor.h"

// Implemented from docs/analise/REA_rodada167f-wavetraveller.md. The float / double types and the operation order are
// the plugin's (its arithmetic is SSE scalar), so FP contraction stays off.
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize ("fp-contract=off")
#endif

namespace lynx::wavetrav
{
namespace
{
constexpr double piD = 3.1415926;    // the plugin's own constant (§3, §5)
constexpr double halfPiD = 1.5707963;
constexpr int ringSize = 256;
constexpr int maxMutes = 64;

std::int32_t i32At (const std::uint8_t* d) noexcept
{
    std::int32_t v;
    std::memcpy (&v, d, 4);
    return v;
}

float f32At (const std::uint8_t* d) noexcept
{
    float v;
    std::memcpy (&v, d, 4);
    return v;
}

void putI32 (std::vector<std::uint8_t>& out, std::int32_t v)
{
    std::uint8_t b[4];
    std::memcpy (b, &v, 4);
    out.insert (out.end(), b, b + 4);
}

void putF32 (std::vector<std::uint8_t>& out, float v)
{
    std::uint8_t b[4];
    std::memcpy (b, &v, 4);
    out.insert (out.end(), b, b + 4);
}

std::uint32_t bitsOf (float x) noexcept
{
    std::uint32_t b;
    std::memcpy (&b, &x, 4);
    return b;
}

// the table writer's clamp, on the bits (sign set → 0; above 1.0, inf and NaN → 1)
float clamp01 (float v) noexcept
{
    const auto b = bitsOf (v);
    if ((b & 0x80000000u) != 0)
        return 0.0f;
    return b < 0x3f800001u ? v : 1.0f;
}

inline std::int64_t roundEven (double x) noexcept { return (std::int64_t) std::nearbyint (x); }

Point pointFromRecord (const std::uint8_t* r) noexcept
{
    Point p;
    std::memcpy (p.raw.data(), r, 60);
    p.x = f32At (r + 8), p.y = f32At (r + 12);
    p.c1x = f32At (r + 0x14), p.c1y = f32At (r + 0x18);
    p.c2x = f32At (r + 0x20), p.c2y = f32At (r + 0x24);
    p.tension = f32At (r + 0x28);
    p.type = i32At (r + 0x30);
    return p;
}

void writeRecord (std::vector<std::uint8_t>& out, const Point& p)
{
    auto r = p.raw;
    auto put = [&r] (int at, const void* v) { std::memcpy (r.data() + at, v, 4); };
    put (8, &p.x), put (12, &p.y), put (0x14, &p.c1x), put (0x18, &p.c1y), put (0x20, &p.c2x), put (0x24, &p.c2y);
    put (0x28, &p.tension), put (0x30, &p.type);
    out.insert (out.end(), r.begin(), r.end());
}

Point makePoint (float x, float y)
{
    Point p;
    p.x = x, p.y = y;
    const std::int32_t one = 1;
    std::memcpy (p.raw.data() + 0x38, &one, 4); // FL's default points carry 1 in their last field
    return p;
}

// one .spln / state point list: count + 60-byte records
bool readPoints (const std::uint8_t*& p, const std::uint8_t* end, std::vector<Point>& out)
{
    if (end - p < 4)
        return false;
    const auto n = i32At (p);
    p += 4;
    if (n < 0 || n > 100000 || end - p < (std::ptrdiff_t) n * 60)
        return false;
    out.clear();
    for (int i = 0; i < n; ++i, p += 60)
        out.push_back (pointFromRecord (p));
    return true;
}

bool readMutes (const std::uint8_t*& p, const std::uint8_t* end, std::vector<Mute>& out)
{
    if (end - p < 4)
        return false;
    const auto n = i32At (p);
    p += 4;
    if (n < 0 || n > 100000 || end - p < (std::ptrdiff_t) n * 8)
        return false;
    out.clear();
    for (int i = 0; i < n; ++i, p += 8)
        out.push_back (Mute { p[0] != 0, f32At (p + 4) });
    return true;
}
} // namespace

// ---- §3: the curve → table builder (FUN_007558c0) ----

void buildTable (const std::vector<Point>& pts, float* table, int& n) noexcept
{
    int w = 1; // the writer's index; every segment starts one back, on the previous segment's last value
    auto put = [&] (float v) {
        if (w >= 0 && w < tableCapacity)
            table[w] = v;
        ++w;
    };
    for (std::size_t k = 0; k + 1 < pts.size(); ++k)
    {
        const auto& p0 = pts[k];
        const auto& p1 = pts[k + 1];
        auto cnt = (int) roundEven ((double) ((p1.x - p0.x) * 1023.0f));
        --w;
        const float y0 = p0.y, y1 = p1.y;
        switch (p0.type)
        {
            case 1: // linear
            {
                auto y = y0;
                for (int i = 0; i < cnt; ++i)
                {
                    put (clamp01 (y));
                    y = y + (y1 - y0) / (float) (cnt - 1);
                }
                break;
            }
            case 2: // cosine (half period)
            {
                const auto d = piD / (double) (cnt - 1);
                const auto dy = (float) ((double) (y1 - y0) * 0.5);
                float ph = 0.0f, y = y0;
                for (int i = 0; i < cnt; ++i)
                {
                    put (clamp01 (y));
                    ph = ph + (float) d;
                    y = (float) ((double) y0 + (double) dy * (std::cos ((double) ph + piD) + 1.0));
                }
                break;
            }
            case 3: // cubic Bezier through the handles (×3), 256 points, read back at even steps of x
            {
                const float c0x = p0.c2x * 3.0f + p0.x, c0y = p0.c2y * 3.0f + p0.y;
                const float c1x = p1.c1x * 3.0f + p1.x, c1y = p1.c1y * 3.0f + p1.y;
                std::array<float, 256> bx {}, by {};
                const auto dt = 1.0 / 255.0;
                double t = 0.0;
                for (int i = 0; i < 256; ++i)
                {
                    const auto a = 1.0 - t;
                    const auto a3 = a * a * a, t3 = t * t * t;
                    const auto b1 = t * 3.0 * a * a, b2 = t * 3.0 * t * a;
                    bx[(std::size_t) i] = (float) (a3 * (double) p0.x + b1 * (double) c0x + b2 * (double) c1x + t3 * (double) p1.x);
                    by[(std::size_t) i] = (float) (a3 * (double) p0.y + b1 * (double) c0y + b2 * (double) c1y + t3 * (double) p1.y);
                    t = (double) (i + 1) * dt;
                }
                int at = 0;
                auto y = y0;
                for (int i = 0; i < cnt; ++i)
                {
                    put (clamp01 (y));
                    const auto fx = ((float) (i + 1) * (p1.x - p0.x)) / (float) (cnt - 1) + p0.x;
                    if (p1.x < fx || ! (fx == fx))
                        break;
                    if (bx[(std::size_t) at] == fx)
                        y = by[(std::size_t) at];
                    else
                    {
                        while (at < 255 && bx[(std::size_t) at] < fx)
                            ++at;
                        if (bx[(std::size_t) at] == fx || at == 0)
                            y = by[(std::size_t) at];
                        else
                        {
                            const auto i1 = (std::size_t) at, i0 = (std::size_t) (at - 1);
                            const auto slope = (float) ((double) (by[i1] - by[i0]) / (double) (bx[i1] - bx[i0]));
                            y = (float) ((double) by[i0] + ((double) fx - (double) bx[i0]) * (double) slope);
                        }
                    }
                }
                break;
            }
            case 4: // sine quarter
            {
                const auto d = halfPiD / (double) (cnt - 1);
                float ph = 0.0f, y = y0;
                for (int i = 0; i < cnt; ++i)
                {
                    put (clamp01 (y));
                    ph = ph + (float) d;
                    y = (float) ((double) y0 + (double) (y1 - y0) * std::sin ((double) ph));
                }
                break;
            }
            case 5: // cosine quarter (ease in)
            {
                const auto d = halfPiD / (double) (cnt - 1);
                float ph = 0.0f, y = y0;
                for (int i = 0; i < cnt; ++i)
                {
                    put (clamp01 (y));
                    ph = ph + (float) d;
                    y = (float) ((double) y0 + (double) (y1 - y0) * (std::cos ((double) ph + piD) + 1.0));
                }
                break;
            }
            case 6: // tension (exponential)
            {
                const auto tn = p0.tension;
                auto y = y0;
                if ((double) tn == 0.5)
                {
                    for (int i = 0; i < cnt; ++i)
                    {
                        put (clamp01 (y));
                        y = y + (y1 - y0) / (float) (cnt - 2);
                    }
                    break;
                }
                const auto a = (1.0f - tn * 2.0f) / (tn * tn);
                const auto d = 1.0 / (double) (cnt - 1);
                const auto lg = std::log ((double) (a + 1.0f));
                float ph = 0.0f;
                for (int i = 0; i < cnt; ++i)
                {
                    ph = ph + (float) d;
                    put (clamp01 (y));
                    y = (float) ((double) y0 + (double) ((y1 - y0) / a) * (std::exp ((double) (ph * (float) lg)) - 1.0));
                }
                break;
            }
            case 8: // silence
                for (int i = 0; i < cnt; ++i)
                    put (0.0f);
                break;
            default: // 0, 7 and others write nothing (the table loses the shared value)
                break;
        }
    }
    n = std::clamp (w - 1, 0, tableCapacity - 1);
}

void Patch::rebuild() noexcept
{
    buildTable (wave, waveTable.data(), waveN);
    buildTable (vol, volTable.data(), volN);
}

Point newPoint (float x, float y)
{
    return makePoint (x, y);
}

Patch defaultPatch()
{
    Patch p;
    p.wave = { makePoint (0.0f, 0.0f), makePoint (1.0f, 1.0f) };
    p.vol = { makePoint (0.0f, 1.0f), makePoint (1.0f, 1.0f) };
    p.rebuild();
    return p;
}

State defaultState()
{
    State s;
    const auto d = defaultPatch();
    for (auto& p : s.patches)
        p = d;
    return s;
}

// ---- §2: the state (FUN_0074c030 / 0074aee0 / 0074b340 / 0074bfc0) ----

bool parseState (const std::vector<std::uint8_t>& bytes, State& state)
{
    state = defaultState();
    const auto* p = bytes.data();
    const auto* end = bytes.data() + bytes.size();
    bool header = false;
    while (end - p >= 8)
    {
        const auto tag = i32At (p), size = i32At (p + 4);
        p += 8;
        if (tag == 3000)
            return header;
        if (tag == 1000)
        {
            // the version word is read and dropped (FUN_0074aee0 overwrites it with the next field): FL loads the
            // 1000001 of its own older presets (Plugin presets\Wave Traveller\Default.fst) like the 1000005 it writes
            if (end - p < 12)
                return false;
            state.selectedKey = i32At (p + 4);
            const auto len = i32At (p + 8);
            p += 12;
            if (len < 0 || end - p < len)
                return false;
            const auto* s = reinterpret_cast<const char*> (p);
            state.samplePath.assign (s, strnlen (s, (std::size_t) len));
            p += len;
            header = true;
        }
        else if (tag == 2000)
        {
            if (end - p < 4 + 24 + 4 + 4)
                return false;
            const auto key = i32At (p);
            if (key < 0 || key >= keyCount)
                return false;
            Patch patch;
            patch.speedBeats = f32At (p + 4), patch.absSpeed = f32At (p + 8);
            patch.attackMs = f32At (p + 12), patch.releaseMs = f32At (p + 16);
            patch.start = f32At (p + 20), patch.end = f32At (p + 24);
            patch.splineType = i32At (p + 28);
            const auto nameLen = i32At (p + 32);
            p += 36;
            if (nameLen < 0 || end - p < nameLen)
                return false;
            patch.name.assign (reinterpret_cast<const char*> (p), (std::size_t) nameLen);
            p += nameLen;
            if (! readMutes (p, end, patch.mutes) || ! readPoints (p, end, patch.wave) || ! readPoints (p, end, patch.vol))
                return false;
            patch.rebuild();
            state.patches[(std::size_t) key] = std::move (patch);
        }
        else
        {
            if (size < 0 || end - p < size)
                return false;
            p += size;
        }
    }
    return header;
}

std::vector<std::uint8_t> writeState (const State& state)
{
    std::vector<std::uint8_t> out;
    const auto len = (std::int32_t) state.samplePath.size();
    putI32 (out, 1000);
    putI32 (out, len + 12);
    putI32 (out, 1000005);
    putI32 (out, state.selectedKey);
    putI32 (out, len);
    out.insert (out.end(), state.samplePath.begin(), state.samplePath.end());
    for (int k = 0; k < keyCount; ++k)
    {
        const auto& pt = state.patches[(std::size_t) k];
        const auto nameLen = (std::int32_t) pt.name.size();
        putI32 (out, 2000);
        // FL's size field (FUN_0074b340): it counts 8 bytes per point; readers parse the record, not this size
        putI32 (out, nameLen + (std::int32_t) (pt.wave.size() + pt.vol.size()) * 8 + (std::int32_t) pt.mutes.size() * 8 + 0x30);
        putI32 (out, k);
        for (auto v : { pt.speedBeats, pt.absSpeed, pt.attackMs, pt.releaseMs, pt.start, pt.end })
            putF32 (out, v);
        putI32 (out, pt.splineType);
        putI32 (out, nameLen);
        out.insert (out.end(), pt.name.begin(), pt.name.end());
        putI32 (out, (std::int32_t) pt.mutes.size());
        for (const auto& m : pt.mutes)
        {
            putI32 (out, m.on ? 1 : 0);
            putF32 (out, m.at);
        }
        putI32 (out, (std::int32_t) pt.wave.size());
        for (const auto& q : pt.wave)
            writeRecord (out, q);
        putI32 (out, (std::int32_t) pt.vol.size());
        for (const auto& q : pt.vol)
            writeRecord (out, q);
    }
    putI32 (out, 3000);
    putI32 (out, 0);
    return out;
}

// ---- §6: .spln presets (FUN_00764dd0 / 007650a0) ----

bool applyPreset (const std::vector<std::uint8_t>& spln, Patch& patch)
{
    if (spln.size() < 40 || i32At (spln.data()) != 4000)
        return false;
    const auto* p = spln.data() + 8; // tag 4000, then the version word the reader skips
    const auto* end = spln.data() + spln.size();
    Patch out = patch;
    out.splineType = i32At (p);
    out.speedBeats = speedFromKnob (f32At (p + 4), false);
    out.absSpeed = speedFromKnob (f32At (p + 8), true);
    out.attackMs = attackFromKnob (f32At (p + 12));
    out.releaseMs = releaseFromKnob (f32At (p + 16));
    out.start = f32At (p + 20);
    out.end = f32At (p + 24);
    p += 28;
    if (! readPoints (p, end, out.wave) || ! readPoints (p, end, out.vol))
        return false;
    std::vector<Mute> mutes;
    if (readMutes (p, end, mutes))
        out.mutes = std::move (mutes);
    else
        out.mutes.clear(); // (FL's writer stores one entry fewer than it counts; such a list reads as empty here)
    out.rebuild();
    patch = std::move (out);
    return true;
}

// ---- the sample (§4) ----

std::shared_ptr<const Sample> sampleFrom (const SampleData& data)
{
    const auto frames = data.frames();
    if (frames == 0)
        return nullptr;
    // FL's host hands the sample at 44.1 kHz (LoadSample's NeededFormat); other rates are converted here linearly [HYP]
    const auto ratio = data.sampleRate > 0.0 ? data.sampleRate / 44100.0 : 1.0;
    const auto length = std::max<std::int64_t> (1, ratio == 1.0 ? (std::int64_t) frames : (std::int64_t) std::floor ((double) frames / ratio));
    auto s = std::make_shared<Sample>();
    s->length = (int) std::min<std::int64_t> (length, 0x7ffffff0);
    s->frames.resize ((std::size_t) (s->length + 1) * 2);
    const auto* l = data.leftData();
    const auto* r = data.rightData();
    auto q16 = [] (float v) { // 16-bit as FL hands it [HYP: round to nearest], then the plugin's /32768
        const auto x = std::clamp (std::nearbyint ((double) v * 32768.0), -32768.0, 32767.0);
        return (float) (x / 32768.0);
    };
    for (int i = 0; i < s->length; ++i)
    {
        float vl, vr;
        if (ratio == 1.0)
            vl = l[i], vr = r[i];
        else
        {
            const auto pos = (double) i * ratio;
            const auto j = (std::size_t) pos;
            const auto f = (float) (pos - (double) j);
            const auto j1 = std::min (j + 1, frames - 1);
            vl = l[j] + (l[j1] - l[j]) * f;
            vr = r[j] + (r[j1] - r[j]) * f;
        }
        s->frames[(std::size_t) i * 2] = q16 (vl);
        s->frames[(std::size_t) i * 2 + 1] = q16 (vr);
    }
    // the guard frame (FUN_00757e00): the first frame's right value twice
    s->frames[(std::size_t) s->length * 2] = s->frames[1];
    s->frames[(std::size_t) s->length * 2 + 1] = s->frames[1];
    return s;
}

// ---- §5: the voices (FUN_00754fd0, FUN_00752740, FUN_00754010) ----

struct Engine::Voice
{
    bool used = false;
    std::uint32_t serial = 0;
    NoteParams note;
    int key = 60;
    double t = 0.0, lastDelta = 1.0;
    float step = 0.0f;
    int s0 = 0, s1 = 0;
    std::array<float, tableCapacity> wave {}, vol {};
    int waveN = 0, volN = 0, vi = 0;
    float curVol = 1.0f;
    int mode = 0; // 0 attack, 1 sustain, 2 release, 3 end fade
    float env = 0.0f, attInc = 0.0f, relDec = 0.0f;
    float lastPos = 0.0f;
    bool gate = true, muteActive = false;
    std::array<Mute, maxMutes> mutes {};
    int muteCount = 0, muteIdx = 0;
    float gL = 0.0f, gR = 0.0f;
    std::uint32_t ringIdx = 0;
    int fadeCnt = 0;
    std::array<float, ringSize> ringL {}, ringR {};
    float dcYL = 0.0f, dcXL = 0.0f, dcYR = 0.0f, dcXR = 0.0f;
};

Engine::Engine() : voices (new Voice[maxVoices]) {}
Engine::~Engine() = default;

void Engine::setRate (int sampleRate) noexcept
{
    rate = std::max (1, sampleRate);
    samplesPerMinute = (float) (rate * 60);
    dcCoef = (float) (1.0 - 126.0 / (double) rate);
    rateConv = 44100.0 / (double) rate; // [HYP] FL's SmpRateConv of the sample it hands at 44.1 kHz
}

int Engine::trigger (const NoteParams& note) noexcept
{
    if (! sample || ! state || sample->length <= 0)
        return -1;
    const auto key = (int) roundEven ((double) ((float) (note.key * 100) / 100.0f));
    if (key < 0 || key >= keyCount)
        return -1; // FL keeps a silent voice that the next render kills
    int slot = -1;
    for (int i = 0; i < maxVoices; ++i)
        if (! voices[(std::size_t) i].used)
        {
            slot = i;
            break;
        }
    if (slot < 0) // all records in use: the oldest goes (FL has no limit)
    {
        slot = order[0];
        kill (slot);
    }
    auto& v = voices[(std::size_t) slot];
    const auto& P = state->patches[(std::size_t) key];
    const auto M = sample->length;
    v = Voice {};
    v.used = true;
    v.serial = ++serials;
    v.note = note;
    v.key = key;
    v.waveN = P.waveN;
    v.volN = P.volN;
    if (P.splineType == 0)
        v.step = (tempo * (float) (v.waveN - 1)) / (P.speedBeats * samplesPerMinute);
    else
        v.step = (float) ((rateConv * (double) P.absSpeed * (double) (v.waveN - 1))
                          / (double) ((float) (M - 1) * (P.end - P.start)));
    const auto d = (note.finalPitch - (float) ((key - 60) * 100)) / 1200.0f;
    if (note.finalPitch < 0.0f)
        v.step = v.step / (1.0f - d);
    else
        v.step = v.step * (d + 1.0f);
    std::copy (P.waveTable.begin(), P.waveTable.end(), v.wave.begin());
    std::copy (P.volTable.begin(), P.volTable.end(), v.vol.begin());
    v.s0 = (int) (double) (P.start * (float) (M - 1));
    v.s1 = (int) (double) (P.end * (float) (M - 1));
    v.attInc = 1000.0f / (P.attackMs * (float) rate);
    v.relDec = 1000.0f / (P.releaseMs * (float) rate);
    v.curVol = v.vol[0];
    flvp::panLaw (note.finalPan, note.finalVol * v.curVol, v.gL, v.gR);
    v.muteCount = (int) std::min<std::size_t> (P.mutes.size(), maxMutes);
    for (int i = 0; i < v.muteCount; ++i)
        v.mutes[(std::size_t) i] = P.mutes[(std::size_t) i];
    v.muteActive = v.muteCount > 0;
    order[(std::size_t) count++] = slot;
    return slot;
}

void Engine::release (int slot) noexcept
{
    if (slot < 0 || slot >= maxVoices || ! voices[(std::size_t) slot].used)
        return;
    auto& v = voices[(std::size_t) slot];
    if (v.mode != 3)
        v.mode = 2;
}

void Engine::kill (int slot) noexcept
{
    if (slot < 0 || slot >= maxVoices)
        return;
    voices[(std::size_t) slot].used = false;
    const auto at = std::find (order.begin(), order.begin() + count, slot);
    if (at != order.begin() + count)
    {
        std::copy (at + 1, order.begin() + count, at);
        --count;
    }
}

void Engine::setVoiceLevels (int slot, float finalVol, float finalPan) noexcept
{
    if (slot < 0 || slot >= maxVoices || ! voices[(std::size_t) slot].used)
        return;
    voices[(std::size_t) slot].note.finalVol = finalVol;
    voices[(std::size_t) slot].note.finalPan = finalPan;
}

void Engine::killAll() noexcept
{
    for (int i = 0; i < maxVoices; ++i)
        voices[(std::size_t) i].used = false;
    count = 0;
}

std::uint32_t Engine::voiceSerial (int slot) const noexcept { return slot >= 0 && slot < maxVoices && voices[(std::size_t) slot].used ? voices[(std::size_t) slot].serial : 0u; }
int Engine::voiceKey (int slot) const noexcept { return slot >= 0 && slot < maxVoices ? voices[(std::size_t) slot].key : -1; }
bool Engine::voiceActive (int slot) const noexcept
{
    return slot >= 0 && slot < maxVoices && voices[(std::size_t) slot].used && voices[(std::size_t) slot].mode < 2;
}
bool Engine::idle() const noexcept { return count == 0; }

void Engine::render (float* out, int n) noexcept
{
    const ScopedFlushDenormals ftz; // FL's audio threads (and the plugin) run with FTZ + DAZ
    if (! sample)
    {
        killAll();
        return;
    }
    // newest first, as the plugin walks its list (the float sums keep that order)
    for (int k = count - 1; k >= 0; --k)
    {
        const auto slot = order[(std::size_t) k];
        renderVoice (voices[(std::size_t) slot], out, n);
        if (! voices[(std::size_t) slot].used)
            kill (slot);
    }
}

void Engine::renderVoice (Voice& v, float* out, int n) noexcept
{
    const auto* smp = sample->frames.data();
    const auto M = sample->length;
    const auto span = v.s1 - v.s0;
    auto dc = [this] (float x, float& y, float& xp) noexcept {
        y = (x - xp) + dcCoef * y;
        xp = x;
        if ((bitsOf (y) & 0x7fffffffu) == 0) // FUN_007514f0 (its threshold compares the bits as an integer)
            y = 0.0f;
        return y;
    };
    for (int i = 0; i < n; ++i)
    {
        if (v.muteActive)
        {
            const auto& m = v.mutes[(std::size_t) v.muteIdx];
            if ((double) m.at <= v.t / (double) (v.waveN - 1))
            {
                v.gate = m.on;
                if (++v.muteIdx == v.muteCount)
                    v.muteActive = false;
            }
        }
        if ((double) v.vi < v.t)
        {
            ++v.vi;
            if (v.vi < v.volN)
                v.curVol = v.vol[(std::size_t) std::min (v.vi, tableCapacity - 1)];
        }
        if (v.mode == 0)
        {
            v.env = v.env + v.attInc;
            if (bitsOf (v.env) > 0x3f800000u)
            {
                v.env = 1.0f;
                v.mode = 1;
            }
        }
        else if (v.mode == 2)
        {
            v.env = v.env - v.relDec;
            if ((bitsOf (v.env) & 0x80000000u) != 0)
            {
                v.used = false;
                return;
            }
        }
        else if (v.mode == 3)
        {
            const auto r = v.ringIdx & 0xffu;
            const auto f = (float) (ringSize - v.fadeCnt);
            out[2 * i] = (float) ((double) out[2 * i] + (double) (f * v.ringL[r]) * 0.00390625);
            out[2 * i + 1] = (float) ((double) out[2 * i + 1] + (double) (f * v.ringR[r]) * 0.00390625);
            ++v.fadeCnt;
            --v.ringIdx;
            if (v.fadeCnt == ringSize)
            {
                v.used = false;
                return;
            }
            continue;
        }

        const auto i0 = std::clamp ((int) v.t, 0, tableCapacity - 2);
        const auto tabPos = ((double) v.wave[(std::size_t) i0]
                             + (v.t - (double) i0) * (double) (v.wave[(std::size_t) i0 + 1] - v.wave[(std::size_t) i0]))
                            * (double) span + (double) v.s0;
        float outL, outR;
        if (renderTaps <= 0)
        {
            // live mode (FUN_00752740): the 6-point polynomial, the volume curve in the gain target
            const auto ip = (int) tabPos;
            float L, R;
            flvp::panLaw (v.note.finalPan, v.note.finalVol, L, R);
            if (! v.gate)
            {
                v.gL = (float) ((double) v.gL * 0.99);
                v.gR = (float) ((double) v.gR * 0.99);
            }
            else
            {
                v.gL = (float) ((double) v.gL * 0.99 + (double) L * 0.01 * (double) v.curVol);
                v.gR = (float) ((double) v.gR * 0.99 + (double) R * 0.01 * (double) v.curVol);
            }
            const auto x = tabPos - (double) ip;
            auto at = [&] (int j, int c) -> float {
                if (j < 0 || (unsigned) j > (unsigned) (M - 1))
                    return 0.0f;
                return smp[(std::size_t) j * 2 + (std::size_t) c];
            };
            for (int c = 0; c < 2; ++c)
            {
                const auto a = at (ip - 2, c), b = at (ip - 1, c), s = at (ip, c), d = at (ip + 1, c), e = at (ip + 2, c), f = at (ip + 3, c);
                const auto A = (double) (a - f) * 0.08333333333333333 + (double) (e - b) * 0.5833333333333334 + (double) (s - d) * 1.3333333333333333;
                const auto B = (((((double) b * 1.25 - (double) s * 2.3333333333333335) + (double) d * 1.6666666666666667) - (double) e * 0.5)
                                 + (double) f * 0.08333333333333333)
                               - (double) a * 0.16666666666666666;
                const auto C = (double) (a - e) * 0.08333333333333333 + (double) (d - b) * 0.6666666666666666;
                const auto y = (float) (((A * x + B) * x + C) * x + (double) s);
                if (c == 0)
                    outL = (v.gL * v.env) * dc (y, v.dcYL, v.dcXL);
                else
                    outR = (v.gR * v.env) * dc (y, v.dcYR, v.dcXR);
            }
        }
        else
        {
            // render mode (FUN_00754010): windowed sinc with `renderTaps` taps per side, its cutoff lowered by the speed
            const auto posF = (float) tabPos;
            const auto ipD = (double) (int) (double) posF;
            float L, R;
            flvp::panLaw (v.note.finalPan + v.note.initPan, v.note.finalVol, L, R);
            if (! v.gate)
            {
                v.gL = (float) ((double) v.gL * 0.99);
                v.gR = (float) ((double) v.gR * 0.99);
            }
            else
            {
                v.gL = (float) ((double) v.gL * 0.99 + (double) L * 0.01);
                v.gR = (float) ((double) v.gR * 0.99 + (double) R * 0.01);
            }
            const auto win = (piD / (double) renderTaps) / 2.0;
            auto fc = 1.0 / std::fabs (v.lastDelta);
            if (1.0 < fc)
                fc = 1.0;
            fc = fc * piD;
            double sL = 0.0, sR = 0.0;
            const auto pd = (double) posF;
            for (int k = 1 - renderTaps; k <= renderTaps - 1; ++k)
            {
                const auto j = (int) (ipD + (double) k);
                if (j < 0 || j >= M)
                    continue;
                auto x = (pd - ipD) - (double) k;
                if (x == 0.0)
                    x = 1e-07;
                const auto w = std::cos (x * win);
                const auto s = std::sin (fc * x);
                const auto c = (s / (x * piD)) * w * w;
                sL = sL + (double) smp[(std::size_t) j * 2] * c;
                sR = sR + (double) smp[(std::size_t) j * 2 + 1] * c;
            }
            outL = (v.gL * v.env) * dc ((float) sL, v.dcYL, v.dcXL);
            outR = (v.gR * v.env) * dc ((float) sR, v.dcYR, v.dcXR);
            v.lastDelta = (double) (posF - v.lastPos);
            v.lastPos = posF;
        }
        const auto r = v.ringIdx & 0xffu;
        v.ringL[r] = outL;
        v.ringR[r] = outR;
        out[2 * i] = out[2 * i] + outL;
        out[2 * i + 1] = out[2 * i + 1] + outR;
        ++v.ringIdx;
        v.t = v.t + (double) v.step;
        if ((double) (v.waveN - 1) < v.t)
        {
            v.mode = 3;
            --v.ringIdx;
        }
    }
}

std::vector<float> renderReference (const State& st, const Sample& smp, const std::vector<RefNote>& notes, int sampleRate,
                                    float tempo, int renderTaps, int length)
{
    Engine e;
    e.setRate (sampleRate);
    e.setTempo (tempo);
    e.setRenderTaps (renderTaps);
    e.setState (std::make_shared<State> (st));
    e.setSample (std::make_shared<Sample> (smp));
    std::vector<float> out ((std::size_t) std::max (0, length) * 2, 0.0f);
    std::vector<int> slots (notes.size(), -1);
    std::vector<std::uint32_t> serial (notes.size(), 0);
    constexpr int block = 512;
    for (int pos = 0; pos < length;)
    {
        auto len = std::min (block, length - pos);
        for (const auto& nt : notes)
        {
            if (nt.start > pos && nt.start < pos + len)
                len = nt.start - pos;
            if (nt.release > pos && nt.release < pos + len)
                len = nt.release - pos;
        }
        for (std::size_t k = 0; k < notes.size(); ++k)
        {
            if (notes[k].start == pos)
            {
                slots[k] = e.trigger (notes[k].note);
                serial[k] = e.voiceSerial (slots[k]);
            }
            if (notes[k].release == pos && slots[k] >= 0 && e.voiceSerial (slots[k]) == serial[k])
                e.release (slots[k]);
        }
        std::vector<float> buf ((std::size_t) len * 2, 0.0f);
        e.render (buf.data(), len);
        for (std::size_t i = 0; i < buf.size(); ++i)
            out[(std::size_t) pos * 2 + i] += buf[i];
        pos += len;
    }
    return out;
}
} // namespace lynx::wavetrav

namespace lynx
{
bool waveTravellerStateUsable (const std::vector<std::uint8_t>& state)
{
    wavetrav::State s;
    return wavetrav::parseState (state, s);
}

namespace
{
// The channel's Wave Traveller. configure() (UI thread) parses the state and hands it over; the audio thread takes it
// at its next note or render and gives the old one back to be freed on the UI thread (as the Granulizer does).
class WaveTravellerInstrument final : public Instrument
{
public:
    explicit WaveTravellerInstrument (std::shared_ptr<const SampleData> s)
    {
        if (s != nullptr)
            engine.setSample (wavetrav::sampleFrom (*s));
        current.state = std::make_shared<wavetrav::State> (wavetrav::defaultState());
        engine.setState (current.state);
        engine.setTempo (140.0f);
    }

    ~WaveTravellerInstrument() override
    {
        delete incoming.exchange (nullptr);
        delete retired.exchange (nullptr);
    }

    void prepare (double sr) noexcept override
    {
        take();
        engine.killAll();
        engine.setRate ((int) std::lround (sr));
        // FL picks the render mode (windowed sinc, its render quality's 256 taps) when it exports, the 6-point
        // polynomial live (Dispatcher ProcessMode, REA §5)
        engine.setRenderTaps (offlineRendering() ? 256 : 0);
    }

    void configure (const ChannelDef& channel, double bpm) noexcept override
    {
        delete retired.exchange (nullptr);
        pitchCents.store (channel.sampler.pitch * 100.0f);
        const auto tempo = (float) (bpm > 0.0 ? bpm : 140.0);
        if (channel.pluginState == lastState && tempo == lastTempo)
            return;
        auto s = std::make_unique<Setup>();
        auto st = std::make_shared<wavetrav::State>();
        if (! wavetrav::parseState (channel.pluginState, *st))
            *st = wavetrav::defaultState();
        s->state = std::move (st);
        s->tempo = tempo;
        lastState = channel.pluginState, lastTempo = tempo;
        delete incoming.exchange (s.release());
    }

    void setNextNoteExpression (float pan, float cents, bool) noexcept override
    {
        nextPan = pan;
        nextCents = cents;
    }

    void noteOn (int key, float velocity) noexcept override
    {
        take();
        const auto vel = std::clamp ((double) velocity, 0.0, 1.0);
        const auto initVol = (float) ((std::pow (11.0, 1.28 * vel) - 1.0) / 10.0);
        const auto k = std::clamp (key, 0, 127);
        wavetrav::NoteParams p;
        p.key = k;
        p.initPan = nextPan;
        p.finalPan = (float) std::clamp ((double) nextPan + channelPan, -1.0, 1.0);
        p.finalVol = (float) (channelVol * (double) initVol);
        p.finalPitch = (float) (100.0 * (k - 60) + (double) pitchCents.load() + (double) nextCents);
        const auto slot = engine.trigger (p);
        if (slot >= 0)
        {
            volFactor[(std::size_t) slot] = initVol;
            notePan[(std::size_t) slot] = nextPan;
            noteKey[(std::size_t) slot] = k;
            slotNote[(std::size_t) slot] = startingNoteId(); // (round 190ej)
        }
        nextPan = 0.0f;
        nextCents = 0.0f;
    }

    void noteOff (int key) noexcept override
    {
        for (int slot = 0; slot < wavetrav::Engine::maxVoices; ++slot)
            if (engine.voiceActive (slot) && endsVoice (noteKey[(std::size_t) slot], slotNote[(std::size_t) slot], key)) // (round 190ej)
                engine.release (slot);
    }

    void allNotesOff() noexcept override
    {
        for (int slot = 0; slot < wavetrav::Engine::maxVoices; ++slot)
            if (engine.voiceActive (slot))
                engine.release (slot);
    }

    void panic() noexcept override { engine.killAll(); }

    bool idle() const noexcept override { return engine.idle(); }

    void render (float* left, float* right, int n, float gl, float gr) noexcept override
    {
        take();
        // FL's channel volume / pan from LYNX's gains (as the Granulizer and 3x Osc)
        const auto vol = std::sqrt (((double) gl * gl + (double) gr * gr) * 0.5);
        auto pan = vol > 0.0 ? std::atan2 ((double) gr, (double) gl) * (4.0 / 3.141592653589793) - 1.0 : 0.0;
        if (std::fabs (pan) < 1e-9)
            pan = 0.0;
        if (vol != channelVol || pan != channelPan)
        {
            channelVol = vol, channelPan = pan;
            for (int slot = 0; slot < wavetrav::Engine::maxVoices; ++slot)
                engine.setVoiceLevels (slot, (float) (vol * (double) volFactor[(std::size_t) slot]),
                                       (float) std::clamp ((double) notePan[(std::size_t) slot] + pan, -1.0, 1.0));
        }
        if (engine.idle())
            return;
        for (int done = 0; done < n;)
        {
            const auto len = std::min (blockSize, n - done);
            std::fill (buf.begin(), buf.begin() + 2 * len, 0.0f);
            engine.render (buf.data(), len);
            for (int j = 0; j < len; ++j)
            {
                left[done + j] += buf[(std::size_t) (2 * j)];
                right[done + j] += buf[(std::size_t) (2 * j + 1)];
            }
            done += len;
        }
    }

private:
    struct Setup
    {
        std::shared_ptr<const wavetrav::State> state;
        float tempo = 140.0f;
    };

    void take() noexcept
    {
        if (retired.load() != nullptr)
            return;
        if (auto* s = incoming.exchange (nullptr))
        {
            std::swap (current.state, s->state); // the old state leaves with `s`, freed on the UI thread
            current.tempo = s->tempo;
            engine.setState (current.state);
            engine.setTempo (current.tempo);
            retired.store (s);
        }
    }

    static constexpr int blockSize = 512;
    wavetrav::Engine engine;
    Setup current;
    std::atomic<Setup*> incoming { nullptr }, retired { nullptr };
    std::atomic<float> pitchCents { 0.0f };
    std::vector<std::uint8_t> lastState;
    float lastTempo = -1.0f;
    std::array<float, 2 * blockSize> buf {};
    std::array<float, wavetrav::Engine::maxVoices> volFactor {}, notePan {};
    std::array<std::uint32_t, wavetrav::Engine::maxVoices> slotNote {}; // each voice slot's LYNX note id (round 190ej)
    std::array<int, wavetrav::Engine::maxVoices> noteKey {};
    double channelVol = 1.0, channelPan = 0.0;
    float nextPan = 0.0f, nextCents = 0.0f;
};
} // namespace

std::unique_ptr<Instrument> makeWaveTravellerInstrument (std::shared_ptr<const SampleData> sample)
{
    return std::make_unique<WaveTravellerInstrument> (std::move (sample));
}
} // namespace lynx
