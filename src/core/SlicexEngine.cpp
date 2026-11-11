#include "core/SlicexEngine.h"

// FL's Slicex voice engine (docs/analise/REA_rodada190h.md Parts 2 and 3). Clean-room: written from the behaviour,
// formulas and layouts described there. The curves (envelopes, LFOs, maps) are the library shared with Fruity Love
// Philter (core/FlCurves.inc); Slicex adds the envelope's ATT / DEC / SUS / REL knobs, the decay point and the release.

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/DelphiMath.h"
#include "core/FlResampler.h"
#include "core/FlVoiceProcessor.h"

#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif

namespace lynx::slicex
{
namespace
{
using u32 = std::uint32_t;

inline float F (double v) noexcept { return (float) v; }
inline long long rnd (double x) noexcept { return (long long) std::nearbyint (x); } // round half to even
inline long long trunc64 (double x) noexcept { return (long long) x; }
inline bool phaseWrap (double x) noexcept
{
    std::uint64_t b;
    std::memcpy (&b, &x, 8);
    return (u32) (b >> 32) > 0x3fefffffu;
}
inline bool hiBelowHalf (double x) noexcept
{
    std::uint64_t b;
    std::memcpy (&b, &x, 8);
    return (u32) (b >> 32) < 0x3fe00000u;
}

constexpr float kTiny = 5.960464477539063e-08f; // 2^-24
const double kPi = 3.1415926535897931;
const double kLn2 = 0.6931471805599453;

// the curve library's point and curve records
struct Pt
{
    float dt = 0, y = 0, t = 0;
    std::uint8_t mode = 0;
    int sign = 0;
    float k = 0, ilk = 0, L = 0, p4 = 0, p5 = 0, p6 = 0, w = 0;
};
struct Env
{
    int kind = 1, ver = 3;
    std::vector<Pt> pts;
    int flags = 0, on = 0, decay = -1, loop = -1, sus = -1;
};

#include "core/FlCurves.inc"

// a state curve as the plugin holds it: levels and times in single precision; an envelope keeps its decay, loop and
// sustain points, an LFO its loop and sustain, a map none (and its x axis is normalised to 1)
enum class CurveUse { envelope, lfo, map };
Curve prepared (const slicex::Curve& s, double offset, CurveUse use)
{
    Curve c;
    float last = 0.0f;
    for (const auto& p : s.points)
    {
        Pt q;
        q.dt = (float) p.dt;
        q.y = std::isnan (p.y) ? last : (float) p.y;
        q.t = p.tension;
        q.mode = p.mode;
        last = q.y;
        c.pts.push_back (q);
    }
    const int n = (int) c.pts.size();
    auto index = [n] (int i) { return i >= 0 && i < n ? i : -1; };
    c.offset = offset;
    c.flags = s.flags;
    c.on = use == CurveUse::map ? 1 : s.enabled;
    c.decay = use == CurveUse::envelope ? index (s.decay) : -1;
    c.loop = use == CurveUse::map ? -1 : index (s.loop);
    c.sus = use == CurveUse::map ? -1 : index (s.sustain);
    if (use == CurveUse::map)
        c.normalise();
    if (c.pts.empty())
        c.pts.push_back (Pt {});
    c.prepare();
    return c;
}

// a map's value at x (0…1): the curve's iterator started and stepped once by x
float mapValue (const Curve& c, double x) noexcept
{
    EnvSt st;
    st.begin (c);
    st.eval (c, x);
    return st.out;
}

// The envelope state: the library's stepping with Slicex's additions — the knobs scale the segment times (ATT before
// the decay point, DEC after it, REL after the note's release when there is a sustain point), SUS lifts the levels
// past the decay point by each point's weight, and a release leaves from the level the envelope has reached
struct SxEnv
{
    int stage = 9, idx = 0, sub = 9;
    double phase = 0, rate = 0, shift = 0;
    float start = 0, delta = 0, out = 0;
    float kA = 1, kD = 1, kR = 1, sus = 0;
    bool hold = false, hasSus = false;

    double level (const Curve& c, int i, bool clip = true) const noexcept
    {
        if (i < 0 || i >= (int) c.pts.size())
            return 0.0;
        const auto& p = c.pts[(std::size_t) i];
        double v = (double) p.y + (double) p.w * shift;
        if (shift != 0.0 && clip)
            v = std::clamp (v, 0.0, 1.0);
        return v - c.offset;
    }

    double timeFactor() const noexcept
    {
        if (stage >= 6)
            return (double) (hasSus ? kR : kD);
        return (double) (sub < 3 ? kA : kD);
    }

    void finish() noexcept
    {
        stage = sub = 9;
        phase = rate = 0.0;
    }

    void begin (const Curve& c, float a, float d, float s, float r) noexcept
    {
        shift = 0.0;
        if (c.on == 0)
        {
            stage = sub = 9;
            start = delta = out = 0.0f;
            phase = rate = 0.0;
            return;
        }
        kA = a, kD = d, kR = r;
        hasSus = c.sus >= 0;
        sus = hasSus ? s : 0.0f;
        idx = 0;
        stage = c.decay < 0 ? 3 : 1;
        if (c.pts[0].dt == 0.0f)
            rate = 1.0, phase = 1.0;
        else
            rate = c.scale / (double) c.pts[0].dt, phase = 0.0;
        sub = stage;
        start = F (level (c, 0));
        delta = 0.0f;
        out = start;
        hold = c.zeroloop || d <= 0.0f;
    }

    void eval (const Curve& c, double adv) noexcept
    {
        const int n = (int) c.pts.size();
        if (! (idx >= 0 && idx <= n - 1))
            return;
        phase += adv * rate;
        while (phaseWrap (phase))
        {
            if (rate == 0.0)
                phase = adv;
            else
                phase = (phase - 1.0) / rate;
            start = F (level (c, idx));
            bool stop;
            if (idx < c.endpt || c.endpt < 0 || stage > 5)
            {
                if (c.decay <= idx && c.decay >= 0 && sub < 3)
                {
                    stage = std::max (stage, 3);
                    sub = 3;
                    shift = (double) sus;
                }
                if (idx < n - 1)
                {
                    ++idx;
                    stop = false;
                }
                else
                {
                    stage = 9;
                    stop = true;
                }
            }
            else if (c.loop < 0)
                stop = true;
            else
            {
                sub = 4;
                idx = c.loop + 1;
                stop = hold;
            }
            delta = F (level (c, idx) - (double) start);
            if (! stop)
            {
                const double dur = (double) c.pts[(std::size_t) idx].dt * timeFactor();
                if (dur <= 0.0)
                {
                    rate = 1.0;
                    phase += 1.0;
                }
                else
                {
                    rate = c.scale / dur;
                    phase *= rate;
                }
            }
            else
                phase = rate = 0.0;
        }
        if (delta == 0.0f)
            out = start;
        else
            out = start + c.shape (idx, phase) * delta;
    }

    void release (const Curve& c) noexcept
    {
        if (stage >= 6)
            return;
        if (c.sus >= 0)
        {
            idx = c.sus;
            shift += (double) out - level (c, c.sus, false);
            phase = 1.0; // the next step starts the segment after the sustain point
        }
        stage = 6;
    }
};

// ------------------------------------------------------------------------------------------------ the program
struct ArtSet
{
    int mode = 1;            // 1 additive (pan, cutoff, resonance, start), 0 multiplicative (volume, speed)
    bool scratch = false;    // the speed articulator's class
    bool envOn = false, lfoOn = false, velOn = false, randOn = false, randBipolar = false;
    Curve env, lfoEnv, mapVel, mapRand;
    float kA = 1, kD = 1, kS = 0, kR = 1;
    LfoWave wave;
    double lfoSpeed = 0, lfoInc = 0;
    u32 phaseOffset = 0;
    float envDepth = 1, lfoDepth = 1;
    std::vector<float> tabX, tabY; // 513 points; empty = the map is at rest
};

struct GroupSet
{
    int type = 0, order = 0;
    float cutOfs = 0, resOfs = 0;
    std::array<ArtSet, targetCount> art;
};

struct DeckSet
{
    DeckAudio audio;
    std::vector<RegionValues> values;
    KeyMap map;
};
} // namespace

struct Program
{
    float masterVol = 1, masterRand = 1, masterLfo = 1, modX = 256, modY = 256;
    int masterPitch = 0, layering = 0, crossfade = 3;
    bool smooth = true, wholeLength = false, loopHalf = false, hq = false, velocityToVolume = true;
    std::array<DeckSet, 2> decks;
    std::array<GroupSet, 8> groups;
};

std::vector<WavRegion> regionsFromMarkers (const std::vector<std::int64_t>& starts, std::int64_t frames)
{
    std::vector<WavRegion> regions;
    for (const auto s : starts)
        if (s >= 0 && s < frames)
        {
            WavRegion r;
            r.start = s;
            regions.push_back (r);
        }
    for (std::size_t i = 0; i < regions.size(); ++i)
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

std::shared_ptr<Program> makeProgram (const State& state, DeckAudio deckA, DeckAudio deckB)
{
    auto p = std::make_shared<Program>();
    p->masterVol = masterVolume (state.masters[mVolume]);
    p->masterRand = masterBipolar (state.masters[mRandom], 256);
    p->masterLfo = masterBipolar (state.masters[mLfo], 128);
    p->masterPitch = state.masters[mPitch];
    p->modX = (float) state.masters[mModX];
    p->modY = (float) state.masters[mModY];
    p->layering = state.optionInt (oLayering);
    p->crossfade = state.optionInt (oCrossfade);
    p->smooth = state.optionByte (oModSmooth) != 0;
    p->wholeLength = state.optionByte (oStartWholeLength) != 0;
    p->loopHalf = state.optionByte (oLoopHalf) != 0;
    p->hq = state.optionByte (oHqEnvelopes) != 0;
    p->velocityToVolume = state.optionByte (oVelocityToVolume) != 0;

    p->decks[0].audio = std::move (deckA);
    p->decks[1].audio = std::move (deckB);
    const bool sameDeck = state.optionByte (oSameDeckOnly) != 0;
    for (int d = 0; d < 2; ++d)
    {
        auto& deck = p->decks[(std::size_t) d];
        if (deck.audio.left == nullptr || deck.audio.frames <= 0)
            deck.audio.regions.clear();
        const auto& records = state.decks[(std::size_t) d].regions;
        for (std::size_t i = 0; i < deck.audio.regions.size(); ++i)
            deck.values.push_back (regionValues (i < records.size() ? records[i] : defaultRegion()));
    }
    for (int d = 0; d < 2; ++d)
        p->decks[(std::size_t) d].map = keyMap (d, p->decks[0].audio.regions, p->decks[1].audio.regions, sameDeck);

    for (std::size_t g = 0; g < 8; ++g)
    {
        const auto& src = state.groups[g];
        auto& dst = p->groups[g];
        dst.type = std::clamp (src.filterType(), 0, 3);
        dst.order = std::clamp (src.filterOrder(), 0, 3);
        dst.cutOfs = groupOffset (src.filterCut());
        dst.resOfs = groupOffset (src.filterRes());
        for (int k = 0; k < targetCount; ++k)
        {
            const auto& a = src.art[(std::size_t) k];
            auto& s = dst.art[(std::size_t) k];
            s.mode = k == volume || k == speed ? 0 : 1;
            s.scratch = k == speed;
            const double rest = s.mode * 0.5;
            s.env = prepared (a.envelope, k == speed ? 0.5 : rest, CurveUse::envelope);
            s.lfoEnv = prepared (a.lfo, 0.5, CurveUse::lfo);
            s.envOn = a.envelope.on();
            s.lfoOn = a.lfo.on();
            s.kA = (float) a.envKnobs[0] * 0.0078125f;
            s.kD = (float) a.envKnobs[1] * 0.0078125f;
            s.kS = (float) a.envKnobs[2] * 0.0078125f;
            s.kR = (float) a.envKnobs[3] * 0.0078125f;
            s.wave.build (a.lfoKnobs[1], a.lfoKnobs[2], a.lfoKnobs[3], (a.lfo.flags & 4) != 0);
            s.phaseOffset = (u32) a.lfoKnobs[4];
            const double x = (double) a.lfoKnobs[0] * 1.52587890625e-05;
            s.lfoSpeed = (double) F (dmath::exp ((double) F (x) * dmath::ln (10001.0)) - 1.0) * 6871947.6736;
            // the group's ENV wheel is the cutoff envelope's depth; the master LFO level is every LFO's depth
            s.envDepth = k == cutoff ? (float) src.filterEnv() * 0.0078125f : a.envDepth;
            s.lfoDepth = k == cutoff ? s.envDepth * p->masterLfo : p->masterLfo;

            auto mapOf = [&] (int which) {
                auto c = prepared (a.maps[(std::size_t) which], rest, CurveUse::map);
                c.scale = 1.0;
                return c;
            };
            auto table = [] (const auto& c) {
                std::vector<float> t;
                if (! c.isDefault())
                {
                    t.resize (513);
                    for (int j = 0; j <= 512; ++j)
                        t[(std::size_t) j] = mapValue (c, (double) j / 512.0);
                }
                return t;
            };
            s.tabX = table (mapOf (mapModX));
            s.tabY = table (mapOf (mapModY));
            s.mapVel = mapOf (mapVelocity);
            s.mapRand = mapOf (mapRandom);
            s.velOn = ! s.mapVel.isDefault();
            s.randOn = ! s.mapRand.isDefault();
            s.randBipolar = rest == 0.5;
        }
    }
    return p;
}

// --------------------------------------------------------------------------------------------------- the engine
namespace
{
constexpr int maxChunk = 256;

struct ArtSt
{
    const ArtSet* set = nullptr;
    SxEnv es, ls;
    u32 lfoPhase = 0;
    float lfoOut = 0;
    double base = 0, out = 0;
};

struct Biquad
{
    double x1[2] {}, x2[2] {}, y1[2] {}, y2[2] {};
};

struct Voice
{
    int state = 10; // 1 playing, 5 released, 9 ending, 10 free
    int note = -1, delay = 0, counter = 0, cutGroup = 0;
    bool first = true, scratch = false, reversed = false, loop = false;
    const DeckSet* deck = nullptr;
    RegionValues rv;
    std::int64_t regionStart = 0, len = 0, loopStart = 0;
    float gain = 1, cents0 = 0;
    float vol = 1, pan = 0, pitch = 0, noteX = 0, noteY = 0;
    double pos = 0, prevPos = 0;
    float fadeThr = 0, fadeInv = 0, speed = 1, baseSpeed = 1;
    float g[4] {}, store[4] {}, from[4] {}, inc[4] {};
    int attack = -1, releaseAt = -1, releaseSkip = 0; // declick counters (−1: off / not releasing)
    bool attackArmed = false;
    float attackOfs[2] {}, releaseLp[2] {};
    std::array<ArtSt, targetCount> art;
    float smX = 256, smY = 256;
    int idxX = 256, idxY = 256;
    const GroupSet* filter = nullptr;
    int stages = 0, type = 0;
    float qExp = 1, bwK = 0;
    std::array<Biquad, 3> bq;
    double b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    flrs::Kernel kernel;
};

inline double combine (double out, double v, int mode) noexcept
{
    return mode == 0 ? out * v : (mode == 1 ? out + v : out * v * 2.0);
}

// FL's 4-gain pan matrix (left, right, right→left, left→right) for a pan −1…1 and a volume
void panMatrix (float p, float v, float* g) noexcept
{
    g[0] = g[1] = g[2] = g[3] = 0.0f;
    if (v == 0.0f)
        return;
    if (p == 0.0f)
    {
        g[0] = g[1] = v;
        return;
    }
    const long double th = (long double) (1.0f + p) * (long double) (kPi / 4.0);
    const float w = v * 1.4142135f;
    const float l = (float) (std::cos (th) * (long double) w), r = (float) (std::sin (th) * (long double) w);
    if (p < 0.0f)
    {
        const float a = 1.0f - r / v;
        g[2] = l * a * 0.5f;
        g[0] = l * (1.0f - a * 0.5f);
        g[1] = r;
    }
    else
    {
        const float b = 1.0f - l / v;
        g[3] = r * b * 0.5f;
        g[1] = r * (1.0f - b * 0.5f);
        g[0] = l;
    }
}

inline double expLaw (double x, double n) noexcept // (float) (exp ((float) x · ln n) − 1)
{
    return (double) F (dmath::exp ((double) F (x) * dmath::ln (n)) - 1.0);
}
} // namespace

struct Engine::Impl
{
    std::shared_ptr<Program> program;
    int sr = 44100, sub = 48, quality = flrs::liveQuality;
    bool rendering = false;
    float bpm = 140.0f, smoothCoef = 0.0f, maxSlope = 0.01f, k44 = 1.0f;
    std::array<Voice, maxVoices> voices;
    std::vector<float> attackTab, relCoef, relGain;
    std::array<float, 2 * maxChunk> work {};
    std::array<float, 1024> tapL {}, tapR {};
    std::array<std::array<std::uint32_t, 128>, 2> cycle {};
    u32 seed = 0x2545f491u;
    int nextNote = 0;

    double random() noexcept // Delphi's generator: a 32-bit LCG scaled to [0, 1)
    {
        seed = seed * 134775813u + 1u;
        return (double) seed * 2.3283064365386963e-10;
    }

    // ---- set-up
    void rateSetup()
    {
        // 48 frames at 44.1 kHz, 4 with "HQ envelopes". (The plugin also reads the host's rendering flag here, but an FL
        // export still steps every 48: FL's render of a plain Slicex project matches to the last bit of its 16-bit
        // file with 48 and not with 4 — a note's release starts on the next sub-block.)
        const bool fine = program != nullptr && program->hq;
        const double base = fine ? 4.0 : 48.0;
        const double D = base * (double) sr / 44100.0;
        sub = std::clamp ((int) std::max<long long> (4, rnd (D / 4.0) * 4), 4, maxChunk);
        const double w = (double) sub * base / (D * 96.0);
        smoothCoef = F (dmath::exp (dmath::ln (0.01) * w / 96.0));
        k44 = (float) (44100.0 / (double) sr);
        maxSlope = 10.0f * (float) (0.001 * (44100.0 / (double) sr));

        // declick: a 4 ms raised-cosine DC cancel at the start, a 20 ms closing low-pass with a 1 ms fade at the end
        const int na = (int) std::max<long long> (1, rnd (0.004 * (double) sr));
        attackTab.resize ((std::size_t) na);
        for (int k = 0; k < na; ++k)
            attackTab[(std::size_t) k] = F ((dmath::cos ((double) (k + 1) * kPi / (double) (na + 1)) + 1.0) * 0.5);
        const int nr = (int) std::max<long long> (1, rnd (0.02 * (double) sr));
        const int m = (int) std::min<long long> (nr, rnd (0.001 * (double) sr));
        relCoef.resize ((std::size_t) nr);
        relGain.resize ((std::size_t) nr);
        const double step = dmath::ln (22050.0) / (kLn2 * (double) nr);
        for (int k = 0; k < nr; ++k)
        {
            relCoef[(std::size_t) k] = F (std::min (1.0, (2.0 / (double) sr) * dmath::exp ((double) (k + 1) * step * kLn2)));
            relGain[(std::size_t) k] = k + 1 <= nr - m ? 1.0f : F ((dmath::cos ((double) (k + 1 - (nr - m)) * kPi / (double) (m + 1)) + 1.0) * 0.5);
        }
        timeScales();
    }

    void timeScales() noexcept
    {
        if (program == nullptr)
            return;
        const double d = (double) sub / (double) sr; // a sub-block in seconds
        const double freeScale = d * 2.0, tempoScale = d * (double) bpm / 60.0;
        for (auto& g : program->groups)
            for (auto& a : g.art)
            {
                a.env.scale = (a.env.flags & 1) ? tempoScale : freeScale;
                a.lfoEnv.scale = (a.lfoEnv.flags & 1) ? tempoScale : freeScale;
                double inc = a.lfoSpeed * a.lfoEnv.scale;
                if (805306368.0 < inc)
                    inc = 805306368.0;
                a.lfoInc = (a.lfoEnv.flags & 8) ? 0.0 : (double) (u32) rnd (inc);
            }
    }

    // ---- voices
    void startRelease (Voice& v, int skip) noexcept
    {
        if (v.releaseAt < 0)
        {
            v.releaseAt = 0;
            v.releaseSkip = skip;
        }
    }

    void cut (Voice& v) noexcept
    {
        v.state = 9;
        for (auto& a : v.art)
        {
            a.es.finish();
            a.ls.finish();
        }
        startRelease (v, 0);
    }

    float layerGain (int i, int i0, double s, int n) const noexcept
    {
        const int mode = program->layering, xf = program->crossfade;
        if (mode <= 0 || mode > 6)
            return 1.0f; // (7, song time: needs the sample's tempo; every layer plays)
        if (mode == 1)
            return 1.0f / (float) n;
        if (xf <= 0)
            return i == i0 ? 1.0f : 0.0f;
        const float t = (float) (1.0 - std::abs (s * (double) (n - 1) - (double) i));
        if (! (t > 0.0f))
            return 0.0f;
        switch (xf)
        {
            case 1: return t;
            case 2: return (float) ((dmath::cos ((double) (1.0f - t) * kPi) + 1.0) * 0.5);
            case 3: return 1.0f - (float) (dmath::exp ((double) (1.0f - t) * dmath::ln (9.0)) - 1.0) * 0.125f;
            default: return t >= 0.5f ? 1.0f : (float) ((dmath::cos ((double) (1.0f - 2.0f * t) * kPi) + 1.0) * 0.5);
        }
    }

    Voice* freeVoice() noexcept
    {
        for (auto& v : voices)
            if (v.state >= 10)
                return &v;
        return nullptr;
    }

    bool startVoice (Voice& v, const Note& n, const DeckSet& deck, int region, float gain, int id, bool reversed) noexcept
    {
        const auto& reg = deck.audio.regions[(std::size_t) region];
        const auto last = std::min (reg.end, deck.audio.frames - 1);
        if (reg.start < 0 || last < reg.start)
            return false;
        v = Voice {};
        v.note = id;
        v.deck = &deck;
        v.rv = deck.values[(std::size_t) region];
        v.cutGroup = v.rv.cutGroup;
        v.regionStart = reg.start;
        v.len = last - reg.start;
        v.gain = v.rv.volume * gain;
        v.cents0 = v.rv.pitchCents + (float) ((60 - n.key) * 100);
        v.vol = n.vol, v.pan = n.pan, v.pitch = n.pitchCents, v.noteX = n.modX, v.noteY = n.modY;
        v.delay = (int) rnd ((double) (((v.rv.delayBeats * (float) sr) * 60.0f) / bpm));
        v.counter = -sub;
        v.state = 1;
        v.reversed = reversed;
        v.baseSpeed = (float) (deck.audio.sampleRate * (1.0 / (double) sr));
        if (reversed)
            v.baseSpeed = -v.baseSpeed;
        v.speed = v.baseSpeed;
        v.fadeThr = (float) (deck.audio.sampleRate * 1.0e-6 * (double) sub);
        v.fadeInv = 1.0f / v.fadeThr;
        v.prevPos = -2147483647.0;
        v.smX = program->modX, v.smY = program->modY;
        v.idxX = (int) std::clamp<long long> (rnd ((double) (v.smX + n.modX * 256.0f)), 0, 512);
        v.idxY = (int) std::clamp<long long> (rnd ((double) (v.smY + n.modY * 256.0f)), 0, 512);
        v.kernel.setQuality (quality);

        const int groupOf[targetCount] = { v.rv.ampGroup, v.rv.ampGroup, v.rv.filterGroup, v.rv.filterGroup, v.rv.speedGroup, v.rv.startGroup };
        for (int k = 0; k < targetCount; ++k)
        {
            auto& a = v.art[(std::size_t) k];
            const int gi = groupOf[k];
            a.set = gi >= 0 && gi < 8 ? &program->groups[(std::size_t) gi].art[(std::size_t) k] : nullptr;
            a.base = k == volume ? 1.0 : 0.0;
            if (a.set == nullptr)
            {
                a.out = a.base;
                continue;
            }
            const auto& s = *a.set;
            if (k == speed && (s.envOn || s.lfoOn))
            {
                a.base = 1.0;
                v.scratch = true;
            }
            if (s.velOn)
                a.base = (double) mapValue (s.mapVel, (double) n.velocity);
            if (s.randOn)
            {
                const double r = (double) mapValue (s.mapRand, random());
                if (s.randBipolar)
                    a.base += r * (double) program->masterRand;
                else
                    a.base *= (r - 1.0) * (double) program->masterRand + 1.0;
            }
            if (k == sampleStart)
            {
                if (! s.tabX.empty())
                    a.base = combine (a.base, (double) s.tabX[(std::size_t) v.idxX], s.mode);
                if (! s.tabY.empty())
                    a.base = combine (a.base, (double) s.tabY[(std::size_t) v.idxY], s.mode);
            }
            a.out = a.base;
            a.es.begin (s.env, s.kA, s.kD, s.kS, s.kR);
            a.ls.begin (s.lfoEnv, 1.0f, 1.0f, 0.0f, 1.0f);
            a.lfoPhase = (u32) (s.wave.startPhase + s.phaseOffset);
        }
        if (v.art[speed].base * (double) v.rv.speedAmount == 0.0)
            v.scratch = false;

        // where the region starts playing: the start wheel and its articulator's base, over 100 ms or the whole region
        double x = (double) v.rv.start + (double) v.rv.startAmount * v.art[sampleStart].base;
        const double span = (double) (v.len + 1);
        if (x <= 0.0)
            x = 0.0;
        else if (program->wholeLength)
            x = std::min (x, 1.0) * span;
        else
            x = x * std::min ((double) sr * 0.1, span);
        if (reversed)
            x = (double) v.len - x;
        v.pos = std::clamp (x, 0.0, span);
        v.attackArmed = v.pos > 0.0;

        if (program->loopHalf)
        {
            v.loop = true;
            v.loopStart = std::clamp<std::int64_t> ((v.len + 1) / 2, 0, v.len);
        }
        if (const int fg = v.rv.filterGroup; fg >= 0 && fg < 8 && program->groups[(std::size_t) fg].order >= 1)
        {
            v.filter = &program->groups[(std::size_t) fg];
            v.stages = v.filter->order;
            v.type = v.filter->type;
            v.qExp = (float) (1.0 / (double) v.stages);
            v.bwK = (float) ((v.type == 2 ? 1.0 / (double) v.stages : (double) v.stages) * (kLn2 / 2.0));
        }
        return true;
    }

    // ---- per sub-block
    void stepArticulator (ArtSt& a, double adv) noexcept
    {
        const auto& s = *a.set;
        if (s.scratch)
        {
            double d = 0.0;
            if (s.envOn)
            {
                a.es.eval (s.env, adv);
                d += (double) a.es.out;
            }
            if (s.lfoOn)
            {
                stepLfo (a, adv);
                d += (double) (a.lfoOut * s.lfoDepth);
            }
            a.out *= d;
            return;
        }
        if (s.envOn)
        {
            a.es.eval (s.env, adv);
            if (s.mode == 1)
                a.out += (double) (a.es.out * s.envDepth);
            else
                a.out *= (double) a.es.out;
        }
        if (s.lfoOn)
        {
            stepLfo (a, adv);
            if (s.mode == 1)
                a.out += (double) (a.lfoOut * s.lfoDepth);
            else
                a.out *= (double) (a.lfoOut * s.lfoDepth * 2.0f + 1.0f);
        }
    }

    void stepLfo (ArtSt& a, double adv) noexcept
    {
        const auto& s = *a.set;
        auto& ls = a.ls;
        double adv2 = adv;
        if (ls.idx == 0 && ls.stage < 9 && ! phaseWrap (ls.phase))
        {
            const double x = adv + (ls.phase - 1.0) / ls.rate;
            adv2 = 0.0 < x ? x : 0.0;
        }
        ls.eval (s.lfoEnv, adv);
        if (ls.idx != 0 || ls.stage > 8 || phaseWrap (ls.phase))
        {
            a.lfoPhase = (u32) (a.lfoPhase + (u32) rnd (adv2 * s.lfoInc));
            a.lfoOut = ls.out * s.wave.value (a.lfoPhase);
        }
    }

    void tick (Voice& v, float panBase, float volBase) noexcept
    {
        double adv;
        if (v.counter < 0)
        {
            v.counter = -v.counter;
            adv = 0.0;
        }
        else
        {
            v.counter = sub;
            adv = 1.0;
        }
        if (v.state >= 5)
            v.state = 9;

        const float tx = program->modX, ty = program->modY;
        v.smX = program->smooth ? tx + (v.smX - tx) * smoothCoef : tx;
        v.smY = program->smooth ? ty + (v.smY - ty) * smoothCoef : ty;
        v.idxX = (int) std::clamp<long long> (rnd ((double) (v.smX + v.noteX * 256.0f)), 0, 512);
        v.idxY = (int) std::clamp<long long> (rnd ((double) (v.smY + v.noteY * 256.0f)), 0, 512);

        for (auto& a : v.art)
        {
            a.out = a.base;
            if (a.set == nullptr)
                continue;
            if (! a.set->tabX.empty())
                a.out = combine (a.out, (double) a.set->tabX[(std::size_t) v.idxX], a.set->mode);
            if (! a.set->tabY.empty())
                a.out = combine (a.out, (double) a.set->tabY[(std::size_t) v.idxY], a.set->mode);
            stepArticulator (a, adv);
            if (v.state > 4)
                v.state = std::min ({ v.state, a.es.stage, a.ls.stage });
        }
        if (v.state > 8)
            startRelease (v, 0);

        const float p = std::clamp ((float) ((double) panBase + v.art[pan].out * 2.0 * (double) v.rv.ampAmount), -1.0f, 1.0f);
        const float vol = (float) ((double) volBase * ((v.art[volume].out - 1.0) * (double) v.rv.ampAmount + 1.0));
        panMatrix (p, vol, v.g);

        if (v.scratch)
        {
            v.speed = (float) ((v.art[speed].out * (double) v.rv.speedAmount + 1.0) * (double) v.baseSpeed);
            if (v.releaseAt < 0)
            {
                // a nearly still read position fades the voice by its movement over the sub-block (signed, as FL does)
                const float d = (float) (v.prevPos - v.pos);
                if (std::abs (d) < v.fadeThr)
                    for (auto& g : v.g)
                        g *= d * v.fadeInv;
            }
            v.prevPos = v.pos;
        }

        if (v.first)
            for (int j = 0; j < 4; ++j)
                v.store[j] = v.g[j];
        for (int j = 0; j < 4; ++j)
        {
            v.from[j] = v.store[j];
            float inc = v.g[j] - v.from[j];
            if (inc != 0.0f)
            {
                v.store[j] = v.g[j];
                inc /= (float) sub;
                if (std::abs (inc) > maxSlope)
                {
                    inc = std::copysign (maxSlope, inc);
                    v.store[j] = v.from[j] + inc * (float) sub;
                }
                if (std::abs (v.store[j]) < kTiny)
                {
                    v.store[j] = 0.0f;
                    inc = -v.from[j] / (float) sub;
                }
            }
            v.inc[j] = inc;
        }
    }

    // the group's filter for this sub-block's cutoff and resonance
    void filterCoefficients (Voice& v) noexcept
    {
        const auto& g = *v.filter;
        const double c = (double) v.rv.cutoff + ((double) g.cutOfs + v.art[cutoff].out) * (double) v.rv.filterAmount;
        const double q = (double) v.rv.resonance + ((double) g.resOfs + v.art[resonance].out) * (double) v.rv.filterAmount;
        const bool band = v.type == 1 || v.type == 2;
        double f = 0.0;
        if (c > 0.0)
        {
            f = expLaw (c, 1501.0) * (v.type == 0 ? 0.0006646666442975402 : 0.00039993334212340415);
            if (band)
                f = std::min (f, 1.0001);
            else if (v.type == 3)
                f = std::min (f, 0.6001);
        }
        double w = (f + (v.type == 0 ? 0.003 : 0.0001)) * (double) k44 * (2.0 * kPi);
        w = std::clamp (w, 0.0, 2.849517146113191);
        double sn, cs;
        dmath::sinCos (w, sn, cs);
        double alpha;
        if (band)
        {
            const double bw = q <= 0.0 ? 0.01 : (q < 1.0 ? expLaw (q, 101.0) * 0.11989999562501907 + 0.01 : 12.0);
            alpha = sn * dmath::sinh (bw * (double) v.bwK * w / sn);
        }
        else
        {
            double Q = q <= 0.0 ? 0.1 : (q < 1.0 ? expLaw (q, 2001.0) * 0.024950001388788223 + 0.1 : 50.0);
            if (v.stages > 1)
                Q = (double) F (dmath::power ((double) F (Q), (double) v.qExp));
            alpha = sn * (0.5 / Q);
        }
        const double a0inv = 1.0 / (1.0 + alpha);
        v.a1 = -2.0 * cs * a0inv;
        v.a2 = (1.0 - alpha) * a0inv;
        switch (v.type)
        {
            case 0:
                v.b1 = (1.0 - cs) * a0inv;
                v.b0 = v.b2 = v.b1 * 0.5;
                break;
            case 1:
                v.b0 = alpha * a0inv;
                v.b1 = 0.0;
                v.b2 = -alpha * a0inv;
                break;
            case 2:
                v.b1 = v.a1;
                v.b0 = v.b2 = a0inv;
                break;
            default:
                v.b1 = -(1.0 + cs) * a0inv;
                v.b0 = v.b2 = -v.b1 * 0.5;
                break;
        }
    }

    void runFilter (Voice& v, int n) noexcept
    {
        for (int s = 0; s < v.stages; ++s)
        {
            auto& b = v.bq[(std::size_t) s];
            for (int ch = 0; ch < 2; ++ch)
            {
                double x1 = b.x1[ch], x2 = b.x2[ch], y1 = b.y1[ch], y2 = b.y2[ch];
                for (int i = 0; i < n; ++i)
                {
                    const double x = (double) work[(std::size_t) (2 * i + ch)];
                    const double y = x * v.b0 + (((x1 * v.b1 - y2 * v.a2) + x2 * v.b2) - y1 * v.a1);
                    x2 = x1, x1 = x;
                    y2 = y1, y1 = y;
                    work[(std::size_t) (2 * i + ch)] = (float) y;
                }
                auto flush = [] (double s0) { return std::abs (s0) < 1.0e-18 ? 0.0 : s0; };
                b.x1[ch] = flush (x1), b.x2[ch] = flush (x2), b.y1[ch] = flush (y1), b.y2[ch] = flush (y2);
            }
        }
    }

    // ---- the sample read: FL's resampler on the region's frames; → frames read before the end
    int read (Voice& v, int n, double speed) noexcept
    {
        const auto& au = v.deck->audio;
        const float* l = au.left + v.regionStart;
        const float* r = (au.right != nullptr ? au.right : au.left) + v.regionStart;
        const auto last = v.len;
        v.kernel.setSpeed (std::abs (speed));
        const auto half = (std::int64_t) v.kernel.halfWidth;
        int done = n;
        for (int i = 0; i < n; ++i)
        {
            auto whole = flrs::wholeFrames (v.pos);
            if (speed >= 0.0 && whole > last)
            {
                if (v.loop && last >= v.loopStart)
                {
                    const double loopLen = (double) (last + 1 - v.loopStart);
                    while (v.pos >= (double) (last + 1))
                        v.pos -= loopLen;
                    whole = flrs::wholeFrames (v.pos);
                }
                else if (v.scratch)
                {
                    v.pos = (double) last;
                    whole = last;
                }
                else
                {
                    std::fill (work.begin() + 2 * i, work.begin() + 2 * n, 0.0f);
                    done = i;
                    break;
                }
            }
            if (v.scratch && v.pos < 0.0)
            {
                v.pos = 0.0;
                whole = 0;
            }
            const double frac = v.pos - (double) whole;
            float& outL = work[(std::size_t) (2 * i)];
            float& outR = work[(std::size_t) (2 * i + 1)];
            if (v.pos < 0.0)
                outL = outR = 0.0f;
            else if (speed == 1.0 && frac == 0.0)
            {
                outL = l[whole];
                outR = r[whole];
            }
            else if (const auto lo = whole - half + 1, hi = whole + half; lo >= 0 && hi <= last)
                flrs::interpolate (v.kernel, l + lo, r + lo, frac, outL, outR);
            else
            {
                for (std::int64_t t = 0; t < 2 * half; ++t)
                {
                    auto q = lo + t;
                    if (v.loop && q > last && last >= v.loopStart)
                        q -= last + 1 - v.loopStart;
                    // frames before the region repeat its first one; past its end the plugin asks for zeros
                    q = std::max<std::int64_t> (q, 0);
                    tapL[(std::size_t) t] = q <= last ? l[q] : 0.0f;
                    tapR[(std::size_t) t] = q <= last ? r[q] : 0.0f;
                }
                flrs::interpolate (v.kernel, tapL.data(), tapR.data(), frac, outL, outR);
            }
            v.pos += speed;
        }
        return done;
    }

    void declick (Voice& v, int n) noexcept
    {
        if (v.attackArmed)
        {
            v.attackArmed = false;
            v.attack = 0;
            v.attackOfs[0] = -work[0];
            v.attackOfs[1] = -work[1];
        }
        if (v.attack >= 0)
        {
            const int na = (int) attackTab.size();
            for (int i = 0; i < n && v.attack < na; ++i, ++v.attack)
            {
                work[(std::size_t) (2 * i)] += attackTab[(std::size_t) v.attack] * v.attackOfs[0];
                work[(std::size_t) (2 * i + 1)] += attackTab[(std::size_t) v.attack] * v.attackOfs[1];
            }
            if (v.attack >= na)
                v.attack = -1;
        }
        if (v.releaseAt >= 0)
        {
            const int nr = (int) relCoef.size();
            const int from = std::min (v.releaseSkip, n);
            v.releaseSkip -= from;
            for (int i = from; i < n; ++i)
            {
                const auto j = (std::size_t) std::min (nr - 1, v.releaseAt);
                ++v.releaseAt;
                for (int ch = 0; ch < 2; ++ch)
                {
                    float& x = work[(std::size_t) (2 * i + ch)];
                    v.releaseLp[ch] += relCoef[j] * (x - v.releaseLp[ch]);
                    x = (x - v.releaseLp[ch]) * relGain[j];
                }
            }
        }
    }

    void renderVoice (Voice& v, float* left, float* right, int n) noexcept
    {
        if (v.delay > 0)
        {
            v.delay -= n;
            if (v.delay >= 0)
                return;
            const int skip = n + v.delay;
            left += skip, right += skip;
            n = -v.delay;
            v.delay = 0;
        }
        const float cents = (v.cents0 + v.pitch) + (float) program->masterPitch;
        const float panBase = v.rv.pan + v.pan;
        const float volBase = (v.gain * v.vol) * program->masterVol;
        while (n > 0)
        {
            if (v.counter <= 0)
                tick (v, panBase, volBase);
            const int len = std::min ({ v.counter, n, maxChunk });
            const double speed = cents == 0.0f && v.speed == 1.0f ? 1.0 : dmath::exp ((double) cents * (kLn2 / 1200.0)) * (double) v.speed;
            const int got = read (v, len, speed);
            if (! v.scratch && v.state < 9)
            {
                if (got < len)
                {
                    v.state = 9;
                    startRelease (v, got);
                }
                else if (v.reversed && v.pos <= 0.0)
                {
                    v.state = 9;
                    startRelease (v, 0);
                }
            }
            if (v.stages > 0)
            {
                filterCoefficients (v);
                runFilter (v, len);
            }
            declick (v, len);

            float g0 = v.from[0], g1 = v.from[1], g2 = v.from[2], g3 = v.from[3];
            for (int i = 0; i < len; ++i)
            {
                const float inL = work[(std::size_t) (2 * i)], inR = work[(std::size_t) (2 * i + 1)];
                left[i] += inL * g0 + inR * g2;
                right[i] += inR * g1 + inL * g3;
                g0 += v.inc[0], g1 += v.inc[1], g2 += v.inc[2], g3 += v.inc[3];
            }
            for (int j = 0; j < 4; ++j)
                v.from[j] += v.inc[j] * (float) len;
            v.first = false;
            v.counter -= len;
            left += len, right += len;
            n -= len;
        }
        if (v.state >= 9 && v.releaseAt >= (int) relCoef.size())
            v.state = 10;
    }
};

Engine::Engine() : d (std::make_unique<Impl>()) { d->rateSetup(); }
Engine::~Engine() = default;

std::shared_ptr<Program> Engine::setProgram (std::shared_ptr<Program> program) noexcept
{
    killAll();
    std::swap (d->program, program);
    d->rateSetup(); // ("HQ envelopes" changes the sub-block; the tables keep their size)
    return program;
}

void Engine::setRate (int sampleRate, bool rendering) noexcept
{
    d->sr = std::clamp (sampleRate, 8000, 768000);
    d->rendering = rendering;
    d->rateSetup();
}

void Engine::setTempo (float bpm) noexcept
{
    d->bpm = bpm > 0.0f ? bpm : 140.0f;
    d->timeScales();
}

void Engine::setInterpolation (int quality) noexcept { d->quality = quality; }

int Engine::trigger (const Note& n) noexcept
{
    if (d->program == nullptr || n.key < 0 || n.key > 127)
        return -1;
    const int deck = n.colour & 1;
    const bool reversed = n.colour > 13;
    const auto& list = d->program->decks[(std::size_t) deck].map[(std::size_t) n.key];
    const int count = (int) list.size();
    if (count == 0)
        return -1;

    // which of the key's regions play, and how loud: the layering mode's selection value and the crossfade
    double s = 0.0;
    int i0 = 0;
    if (count >= 2)
    {
        switch (d->program->layering)
        {
            case 2: s = (double) n.velocity; break;
            case 3: s = (double) d->program->modX * (1.0 / 512.0); break;
            case 4: s = (double) d->program->modY * (1.0 / 512.0); break;
            case 5: s = d->random(); break;
            case 6:
            {
                auto& c = d->cycle[(std::size_t) deck][(std::size_t) n.key];
                s = (double) (c % (u32) count) / (double) (count - 1);
                ++c;
                break;
            }
            default: break;
        }
        i0 = (int) std::clamp<long long> (rnd (s * (double) count - 0.5), 0, count - 1);
    }

    const int id = d->nextNote;
    d->nextNote = (d->nextNote + 1) & 0x3fffffff;
    bool any = false;
    for (int i = 0; i < count; ++i)
    {
        const auto& src = d->program->decks[(std::size_t) ((list[(std::size_t) i] >> 16) & 1)];
        const int region = list[(std::size_t) i] & 0xffff;
        if (region >= (int) src.audio.regions.size())
            continue;
        const float gain = count >= 2 ? d->layerGain (i, i0, s, count) : 1.0f;
        if (const int group = src.values[(std::size_t) region].cutGroup; group > 0)
            for (auto& v : d->voices)
                if (v.state < 10 && v.cutGroup == group && ! v.first)
                    d->cut (v);
        if (! (gain > kTiny))
            continue;
        if (auto* v = d->freeVoice())
            any = d->startVoice (*v, n, src, region, gain, id, reversed) || any;
    }
    return any ? id : -1;
}

void Engine::release (int id) noexcept
{
    for (auto& v : d->voices)
        if (v.state < 10 && v.note == id)
        {
            if (v.delay > 0)
            {
                v.state = 10; // released before its delay ran out: no sound
                continue;
            }
            v.state = std::max (v.state, 5);
            for (auto& a : v.art)
                if (a.set != nullptr)
                {
                    a.es.release (a.set->env);
                    a.ls.release (a.set->lfoEnv);
                }
        }
}

void Engine::releaseAll() noexcept
{
    for (auto& v : d->voices)
        if (v.state < 5)
            release (v.note);
}

void Engine::killAll() noexcept
{
    for (auto& v : d->voices)
        v.state = 10;
}

void Engine::setNoteLevels (int id, float vol, float pan, float pitchCents) noexcept
{
    for (auto& v : d->voices)
        if (v.state < 10 && v.note == id)
            v.vol = vol, v.pan = pan, v.pitch = pitchCents;
}

bool Engine::noteActive (int id) const noexcept
{
    return std::any_of (d->voices.begin(), d->voices.end(), [id] (const Voice& v) { return v.state < 10 && v.note == id; });
}

bool Engine::idle() const noexcept
{
    return std::none_of (d->voices.begin(), d->voices.end(), [] (const Voice& v) { return v.state < 10; });
}

int Engine::voiceCount() const noexcept
{
    return (int) std::count_if (d->voices.begin(), d->voices.end(), [] (const Voice& v) { return v.state < 10; });
}

void Engine::render (float* left, float* right, int frames) noexcept
{
    if (d->program == nullptr || frames <= 0)
        return;
    const flvp::X87Extended x87; // FL's interpolation kernels run at 64-bit x87 precision
    // newest voice first, as the plugin walks its list
    for (int i = maxVoices - 1; i >= 0; --i)
        if (d->voices[(std::size_t) i].state < 10)
            d->renderVoice (d->voices[(std::size_t) i], left, right, frames);
}
} // namespace lynx::slicex
