// FL Studio's Fruit Kick (REA_rodada43 §1): a 1:1 port of the round-43 reference model. Float arithmetic follows the
// plugin's operation order; doubles where the plugin computes in double; long double for FL's x87 pan law.

#include "core/FruitKick.h"
#include "core/FlVoiceProcessor.h"
#include "core/FlangusFastDist.h"
#include "core/DrumParamRanges.h"
#include "core/EngineParams.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>

// the float/double operation order follows the plugin's, so fusing a·b + c into one FMA would change the results:
// contraction stays off.
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize ("fp-contract=off")
#endif

namespace lynx
{
namespace
{
constexpr double kPi = 3.1415926535897931;

std::int32_t i32At (const std::vector<std::uint8_t>& d, std::size_t o)
{
    std::int32_t v = 0;
    if (o + 4 <= d.size())
        std::memcpy (&v, d.data() + o, 4);
    return v;
}

long rint64 (double x) { return std::lrint (x); } // cvtsd2si: round half to even

// ---- tables: FL's host sine (WaveTables[0]), the plugin's LFO triangle/square ----
struct Tables
{
    std::array<float, 16384> sine {}, tri {}, square {};

    Tables()
    {
        for (int i = 0; i < 16384; ++i)
            sine[(std::size_t) i] = (float) std::sin ((double) (2 * i) * kPi / 16384.0);
        for (int i = 0; i < 0x2000; ++i)
            tri[(std::size_t) ((i + 0x3000) & 0x3fff)] = (float) ((double) (i << 2) / 16384.0 + -1.0);
        for (int i = 0x2000; i < 0x4000; ++i)
            tri[(std::size_t) ((i + 0x3000) & 0x3fff)] = (float) (1.0 - (double) ((i - 0x2000) * 4) / 16384.0);
        for (int i = 0; i < 16384; ++i)
            square[(std::size_t) i] = i < 0x2000 ? 1.0f : -1.0f;
    }
};

const Tables& tables()
{
    static const Tables t;
    return t;
}

// Delphi Power(single, single) as the plugin calls it (FUN_005ea930)
float fpower (float x, float y)
{
    if (y == 0.0f)
        return 1.0f;
    if (x == 0.0f && y > 0.0f)
        return 0.0f;
    if ((float) (long long) y == y && std::fabs (y) < 2147483648.0f)
    {
        const auto n = (long long) y;
        double r = 1.0;
        for (long long i = 0; i < (n < 0 ? -n : n); ++i)
            r *= (double) x;
        return (float) (n >= 0 ? r : 1.0 / r);
    }
    return (float) std::exp ((double) y * std::log ((double) x));
}

struct Params
{
    int p[6] {};
    float e4 = 0.0f, r154 = 1.0f, fdec = 1.0f, adec = 1.0f, dry = 1.0f, wet = 0.0f;
    std::int32_t floor = 0;

    long incOf (double cents) const { return rint64 ((double) e4 * std::exp (cents * (std::log (2.0) / 1200.0))); }

    void init (const std::vector<std::uint8_t>& state, double fs)
    {
        for (int i = 0; i < 6; ++i)
            p[i] = i32At (state, 4 + 4 * (std::size_t) i);
        derive (fs);
    }

    void derive (double fs)
    {
        e4 = (float) (2247346493527.166 / fs); // 2³² · 523.25 Hz / fs
        r154 = (float) (44100.0 / fs);
        const double ln1001 = std::log (1001.0);
        floor = (std::int32_t) incOf ((double) (p[1] - 3600));
        auto expPart = [&] (int v) { return std::exp ((double) (1.0f - (float) v * 0.00390625f) * ln1001); };
        fdec = fpower (0.9997f - (float) (expPart (p[2]) - 1.0) * 9.7e-6f, r154);
        adec = fpower ((float) (0.99998 - (double) (float) (expPart (p[3]) - 1.0) * 4.98e-6), r154);
        const float d = (float) p[5] * 0.0078125f;
        dry = 1.0f - d;
        wet = (float) ((double) d * 3.3);
    }
};

struct Voice
{
    std::uint32_t phase = 0;
    float inc = 0.0f, amp = 1.5f;
    int key = 60;
    double initVol = 1.0, notePan = 0.0;
    std::uint32_t lynxNote = 0; // the LYNX note that started it (round 190ej)
    float fvol = 1.0f, fpan = 0.0f;
    bool released = false; // the framework's "released" flag (+0x174)
    int id = 0;
    flvp::Proc pr;         // the plugin voice processor shared with DX10 (core/FlVoiceProcessor.h)
};

class Engine
{
public:
    static constexpr int maxBlock = 4096, maxVoices = 64, hostBlock = 512;

    bool init (const std::vector<std::uint8_t>& state, double fs, double tempo, int ppq)
    {
        if (! fruitKickStateUsable (state))
            return false;
        P.init (state, fs);
        this->fs = fs;
        // the end fade: 441 entries at every rate (built while the framework's rate object still holds 44 100 Hz, as
        // in DX10) with IPP's cosine (REA_rodada74 §1-2)
        const auto& table = flvp::endTable();
        fade.assign (table.begin(), table.end());
        // envelope snapshot (version 1, ≥ 364 B) or the framework's defaults
        const auto defaults = flvp::defaultSnapshot();
        const bool snap = i32At (state, 0) == 1 && state.size() >= 32 + 364;
        S.build (snap ? state.data() + 32 : defaults.data(), ppq, tempo);
        voices.reserve (maxVoices);
        return true;
    }

    // a live knob change: the decays / floor / distortion apply to the sounding kicks, the start pitch and click
    // phase to the next ones
    void setParam (int i, int v)
    {
        if (i < 0 || i >= 6 || P.p[i] == v)
            return;
        P.p[i] = v;
        P.derive (fs);
    }

    Voice* trigger (int key, double initVol, float fvol, float fpan, double pitchCents)
    {
        // max polyphony (FUN_0072e400): the first voice not yet released gets the end fade
        if (maxPoly > 0 && (int) voices.size() >= maxPoly)
        {
            Voice* v = nullptr;
            for (auto& w : voices)
            {
                v = &w;
                if (! w.released)
                    break;
            }
            if (v != nullptr)
                startFade (*v);
        }
        if ((int) voices.size() >= maxVoices)
            voices.erase (voices.begin());
        Voice v;
        v.id = nextId++;
        v.key = key;
        v.initVol = initVol;
        v.phase = (std::uint32_t) P.p[4] << 24;
        const auto fpitch = (float) (100.0 * key - 6000.0 + pitchCents);
        const float c = ((float) P.p[0] + fpitch * 0.5f) + 1200.0f;
        const auto lo = (float) (P.p[1] - 3600);
        float st = lo;
        if (lo <= c)
            st = 4800.0f < c ? 4800.0f : c;
        v.inc = (float) P.incOf ((double) st);
        v.amp = 1.5f;
        v.fvol = fvol;
        v.fpan = fpan;
        v.pr.init (S);
        voices.push_back (v);
        return &voices.back();
    }

    void release (Voice& v)
    {
        v.released = true;
        for (int k = 1; k < 5; ++k)
            v.pr.arts[(std::size_t) k].release (S.recs[(std::size_t) k]);
        if (S.recs[1].enabled == 0)
            startFade (v);
    }

    // `note` ≠ 0 (round 190ej): only that LYNX note's voice (FL's Voice_Release of one voice, REA_rodada43 §1)
    void releaseKey (int key, std::uint32_t note = 0)
    {
        for (auto& v : voices)
            if (v.key == key && ! v.released && (note == 0 || v.lynxNote == note))
                release (v);
    }

    void killAll() { voices.clear(); }
    bool quiet() const { return voices.empty(); }

    void setLevels (double chanVol, double chanPan)
    {
        for (auto& v : voices)
        {
            v.fvol = (float) (v.initVol * chanVol);
            v.fpan = (float) (v.notePan + chanPan);
        }
    }

    // one host block (n ≤ maxBlock): overwrites l/r (silence when no voice is alive)
    void block (float* l, float* r, int n)
    {
        std::fill (l, l + n, 0.0f);
        std::fill (r, r + n, 0.0f);
        if (voices.empty())
            return;
        float mono[maxBlock];
        for (std::size_t vi = 0; vi < voices.size();)
        {
            auto& v = voices[vi];
            auto& pr = v.pr;
            if (tick || pr.first > 0)
                pr.control (S); // (the pitch target does not reach Fruit Kick: it reads Final.Pitch only at the trigger)
            const bool finished = renderVoice (v, mono, n);
            for (int j = 0; j < n; ++j)
                bufA[2 * (std::size_t) j] = bufA[2 * (std::size_t) j + 1] = mono[j];
            float tL, tR;
            flvp::panLaw (v.fpan + pr.pan, v.fvol * pr.vol, tL, tR);
            if (! pr.gainsSet)
                pr.gL = pr.gR = 0.0f, pr.gainsSet = true;
            if (pr.first > 1)
                pr.gL = tL, pr.gR = tR; // the first block starts at the target
            float* x = bufA.data();
            if (flvp::filter (S, pr, bufA.data(), bufB.data(), n, tL, tR))
                x = bufB.data();
            if (pr.endPos >= 0) // end fade (FUN_0071e740, mode 3): the table, then silence
            {
                const int N = (int) fade.size();
                const int k = std::min (std::max (N - pr.endPos, 0), n);
                for (int j = 0; j < k; ++j)
                {
                    const auto t = fade[(std::size_t) (pr.endPos + j)];
                    x[2 * j] = x[2 * j] * t;
                    x[2 * j + 1] = x[2 * j + 1] * t;
                }
                std::fill (x + 2 * k, x + 2 * n, 0.0f);
                pr.endPos += n;
            }
            const float dl = flvp::rampStep (tL, pr.gL, n), dr = flvp::rampStep (tR, pr.gR, n);
            if (dl == 0.0f && dr == 0.0f)
            {
                for (int j = 0; j < n; ++j)
                {
                    l[j] = x[2 * j] * tL + l[j];
                    r[j] = x[2 * j + 1] * tR + r[j];
                }
            }
            else
            {
                // FL's SSE mixer: gains (L, R, L + dL, R + dR) stepped by 2·d every two frames, then a per-frame tail
                const int m8 = n / 8 * 8;
                float g0 = pr.gL, g1 = pr.gR, g2 = pr.gL + dl, g3 = pr.gR + dr;
                const float d2l = dl + dl, d2r = dr + dr;
                for (int j = 0; j < m8; j += 2)
                {
                    l[j] = x[2 * j] * g0 + l[j];
                    r[j] = x[2 * j + 1] * g1 + r[j];
                    l[j + 1] = x[2 * j + 2] * g2 + l[j + 1];
                    r[j + 1] = x[2 * j + 3] * g3 + r[j + 1];
                    g0 = g0 + d2l, g1 = g1 + d2r, g2 = g2 + d2l, g3 = g3 + d2r;
                }
                for (int j = m8; j < n; ++j)
                {
                    l[j] = x[2 * j] * g0 + l[j];
                    r[j] = x[2 * j + 1] * g1 + r[j];
                    g0 = g0 + dl, g1 = g1 + dr;
                }
            }
            pr.gL = tL, pr.gR = tR;
            pr.first = 0;
            // finished voices leave the plugin's list in order (Delphi TList.Delete)
            if (finished || pr.endPos >= (int) fade.size())
                voices.erase (voices.begin() + (std::ptrdiff_t) vi);
            else
                ++vi;
        }
        tick = false;
    }

    Params P;
    double fs = 44100.0;
    flvp::Settings S;
    std::vector<float> bufA = std::vector<float> (2 * maxBlock), bufB = std::vector<float> (2 * maxBlock);
    std::vector<float> fade;
    std::vector<Voice> voices;
    bool tick = false;
    // the host's per-tick callback (slot 14): the next block steps the articulators; global LFO phases advance now
    void onTick()
    {
        tick = true;
        for (auto& r : S.recs)
            if ((r.flags & 0x20) != 0)
                r.globalPhase += r.lfoInc;
    }
    int maxPoly = 0, nextId = 0;

private:
    void startFade (Voice& v)
    {
        v.released = true;
        if (v.pr.endPos < 0)
            v.pr.endPos = 0;
    }

    bool renderVoice (Voice& v, float* buf, int n)
    {
        const auto& t = tables();
        auto ph = v.phase;
        auto inc = v.inc, amp = v.amp;
        const auto floor = (float) P.floor;
        for (int i = 0; i < n; ++i)
        {
            buf[i] = t.sine[ph >> 18] * amp;
            ph += (std::uint32_t) rint64 ((double) inc);
            amp = amp * P.adec;
            if (floor < inc)
                inc = inc * P.fdec;
            else
                inc = floor;
        }
        v.phase = ph, v.inc = inc, v.amp = amp;
        std::int32_t ab, lim;
        const float lo = 0.0002f;
        std::memcpy (&ab, &amp, 4);
        std::memcpy (&lim, &lo, 4);
        if (P.p[5] > 0) // FL's host waveshaper: curve set 0, curve 10, drive 0.1 (REA_rodada55 §2.3: FL's x87 routines, the
                        // interpolating one while FL renders a file, the live one otherwise)
            fastdistfx::hostShaper (buf, n, 0, 10, P.dry, P.wet, 0.1f, offlineRendering());
        return ab < lim;
    }
};

class FruitKickInstrument final : public Instrument
{
public:
    ~FruitKickInstrument() override
    {
        delete incoming.exchange (nullptr);
        delete retired.exchange (nullptr);
    }

    void prepare (double sr) noexcept override
    {
        delete retired.exchange (nullptr);
        rate.store (sr);
        rebuild();
        take();
    }

    void configure (const ChannelDef& channel, double bpm) noexcept override
    {
        delete retired.exchange (nullptr);
        channelCents.store (channel.sampler.pitch * 100.0f);
        mono.store (channel.maxPolyphony == 1); // FL tells the generator its Max polyphony, never the Mono box (round 168l: 0xf6f240 → 0xfed7c0)
        const auto b = bpm > 0.0 ? bpm : 120.0;
        if (channel.pluginState == state && b == tempo.load())
            return;
        // a knob turned on the page (round 168p): only the six parameters differ → the running engine takes them,
        // the sounding kicks keep playing
        if (b == tempo.load() && incoming.load() == nullptr && channel.pluginState.size() == state.size() && fruitKickStateUsable (state)
            && fruitKickStateUsable (channel.pluginState) && std::memcmp (state.data(), channel.pluginState.data(), 4) == 0
            && std::memcmp (state.data() + 28, channel.pluginState.data() + 28, state.size() - 28) == 0)
        {
            std::vector<std::pair<int, float>> changed;
            for (int i = 0; i < 6; ++i)
                if (fruitKickParam (state, i) != fruitKickParam (channel.pluginState, i))
                    changed.push_back ({ i, (float) fruitKickParam (channel.pluginState, i) });
            if (changed.size() <= liveParams.space())
            {
                for (const auto& [i, v] : changed)
                    liveParams.push (i, v);
                state = channel.pluginState;
                return;
            }
        }
        state = channel.pluginState;
        tempo.store (b);
        rebuild();
    }

    void setNextNoteExpression (float pan, float cents, bool) noexcept override
    {
        nextPan = pan;
        nextCents = cents;
    }

    // automation (round 168p): the clip's 0…1 over the knob's range, on the running engine
    void setParameter (int index, float value) noexcept override
    {
        take();
        const auto r = drumranges::fruitKick (index);
        if (engine != nullptr && r.valid && std::isfinite (value))
            engine->setParam (index, (int) r.raw (value));
    }

    void noteOn (int key, float velocity) noexcept override
    {
        take();
        if (engine == nullptr)
            return;
        engine->maxPoly = mono.load() ? 1 : 0;
        const auto vel = std::clamp ((double) velocity, 0.0, 1.0);
        const auto initVol = (std::pow (11.0, 1.28 * vel) - 1.0) / 10.0; // FL's Init.Vol for a velocity (REA_rodada28 §4.1)
        auto* v = engine->trigger (std::clamp (key, 0, 127), initVol, (float) (initVol * channelVol),
                                   (float) ((double) nextPan + channelPan), (double) channelCents.load() + (double) nextCents);
        v->notePan = nextPan;
        v->lynxNote = startingNoteId();
        nextPan = 0.0f, nextCents = 0.0f;
    }

    void noteOff (int key) noexcept override
    {
        take();
        if (engine != nullptr)
            engine->releaseKey (key, endingNoteId());
    }

    void allNotesOff() noexcept override
    {
        take();
        if (engine != nullptr)
            for (auto& v : engine->voices)
                if (! v.released)
                    engine->release (v);
    }

    bool idle() const noexcept override { return engine == nullptr || engine->quiet(); }

    void render (float* left, float* right, int n, float gl, float gr) noexcept override
    {
        take();
        if (engine == nullptr || n <= 0)
            return;
        const auto vol = std::sqrt (((double) gl * gl + (double) gr * gr) * 0.5);
        auto pan = vol > 0.0 ? std::atan2 ((double) gr, (double) gl) * (4.0 / kPi) - 1.0 : 0.0;
        if (std::fabs (pan) < 1e-9)
            pan = 0.0;
        if (vol != channelVol || pan != channelPan)
        {
            channelVol = vol, channelPan = pan;
            engine->setLevels (vol, pan);
        }
        // the framework steps its envelopes once per sequencer tick (96 PPQ of the song tempo)
        const auto perTick = rate.load() * 60.0 / (tempo.load() * 96.0);
        int done = 0;
        while (done < n)
        {
            int m = std::min (Engine::hostBlock, n - done);
            if (tickIn <= 0.0)
            {
                engine->onTick();
                tickIn += perTick;
            }
            const auto untilTick = (int) std::ceil (tickIn);
            if (untilTick > 0 && untilTick < m)
                m = untilTick;
            if (! engine->quiet())
            {
                engine->block (bufL, bufR, m);
                for (int i = 0; i < m; ++i)
                {
                    left[done + i] += bufL[i];
                    right[done + i] += bufR[i];
                }
            }
            tickIn -= m;
            done += m;
        }
    }

private:
    void rebuild() noexcept
    {
        try
        {
            auto e = std::make_unique<Engine>();
            if (! e->init (state, rate.load(), tempo.load(), 96))
                e.reset();
            delete incoming.exchange (e ? e.release() : new Engine());
        }
        catch (...)
        {
        }
    }

    void take() noexcept
    {
        if (retired.load() != nullptr)
            return;
        if (auto* e = incoming.exchange (nullptr))
        {
            // a new state or rate starts a new engine (FL recreates the plugin's voices on a state load)
            retired.store (engine);
            engine = e->fade.empty() ? nullptr : e; // an empty engine stands for an unusable state
            if (engine == nullptr)
                delete e;
            liveParams.clear(); // (the new engine was built from the newest state)
        }
        if (engine != nullptr)
            liveParams.drain ([this] (int i, float v) { engine->setParam (i, (int) v); });
    }

    LiveParamQueue liveParams;

    Engine* engine = nullptr;
    std::atomic<Engine*> incoming { nullptr }, retired { nullptr };
    std::atomic<double> rate { 44100.0 }, tempo { 120.0 };
    std::atomic<float> channelCents { 0.0f };
    std::atomic<bool> mono { false };
    std::vector<std::uint8_t> state;
    double channelVol = 1.0, channelPan = 0.0, tickIn = 0.0;
    float nextPan = 0.0f, nextCents = 0.0f;
    float bufL[Engine::maxBlock] {}, bufR[Engine::maxBlock] {};
};
} // namespace

bool fruitKickStateUsable (const std::vector<std::uint8_t>& state)
{
    const auto v = i32At (state, 0);
    return (v == 0 || v == 1) && state.size() >= 4 + 6 * 4;
}

std::vector<std::uint8_t> fruitKickWithChannelEnvelope (const std::vector<std::uint8_t>& state,
                                                        const std::vector<std::vector<std::uint8_t>>& envelopeRecords,
                                                        int cutoff, int resonance, int filterType)
{
    if (i32At (state, 0) != 0 || state.size() < 28 || envelopeRecords.size() < 5)
        return state;
    for (int k = 0; k < 5; ++k)
        if (envelopeRecords[(std::size_t) k].size() < 68)
            return state;
    std::vector<std::uint8_t> out (400, 0);
    const std::int32_t one = 1;
    std::memcpy (out.data(), &one, 4);
    std::memcpy (out.data() + 4, state.data() + 4, 24);
    for (int k = 0; k < 5; ++k)
        std::memcpy (out.data() + 32 + 68 * k, envelopeRecords[(std::size_t) k].data(), 68);
    std::memcpy (out.data() + 32 + 0x15c, &cutoff, 4);
    std::memcpy (out.data() + 32 + 0x160, &resonance, 4);
    std::memcpy (out.data() + 32 + 0x168, &filterType, 4);
    return out;
}

std::vector<std::string> fruitKickFallbacks (const std::vector<std::uint8_t>& state)
{
    if (! fruitKickStateUsable (state))
        return { "unreadable state" };
    return {};
}

int fruitKickParam (const std::vector<std::uint8_t>& state, int index)
{
    return fruitKickStateUsable (state) && index >= 0 && index < 6 ? i32At (state, 4 + 4 * (std::size_t) index) : 0;
}

bool fruitKickSetParam (std::vector<std::uint8_t>& state, int index, int value)
{
    if (! fruitKickStateUsable (state) || index < 0 || index >= 6)
        return false;
    std::memcpy (state.data() + 4 + 4 * index, &value, 4);
    return true;
}

std::unique_ptr<Instrument> makeFruitKickInstrument() { return std::make_unique<FruitKickInstrument>(); }

namespace fruitkick
{
std::vector<float> renderReference (const std::vector<std::uint8_t>& state, const std::vector<RefNote>& notes, double sampleRate,
                                    int length, int block, const std::vector<int>& ticks, double tempo, int ppq)
{
    Engine e;
    if (length <= 0 || ! e.init (state, sampleRate, tempo, ppq))
        return {};
    std::vector<int> evs (ticks);
    for (const auto& nt : notes)
    {
        evs.push_back (nt.start);
        if (nt.release >= 0)
            evs.push_back (nt.release);
    }
    block = std::clamp (block, 1, Engine::maxBlock);
    std::vector<float> out (2 * (std::size_t) length, 0.0f);
    std::vector<int> id (notes.size(), -1);
    float l[Engine::maxBlock], r[Engine::maxBlock];
    int t = 0;
    while (t < length)
    {
        for (std::size_t i = 0; i < notes.size(); ++i)
        {
            const auto& nt = notes[i];
            if (nt.start == t)
                id[i] = e.trigger (nt.key, 1.0, (float) nt.finalVol, (float) nt.finalPan, nt.pitchCents)->id;
            if (nt.release == t && id[i] >= 0)
                for (auto& v : e.voices)
                    if (v.id == id[i] && ! v.released)
                        e.release (v);
        }
        for (auto tk : ticks)
            if (tk == t)
                e.onTick();
        auto m = std::min (block, length - t);
        for (auto ev : evs)
            if (t < ev && ev < t + m)
                m = ev - t;
        if (! e.voices.empty())
        {
            e.block (l, r, m);
            for (int j = 0; j < m; ++j)
            {
                out[2 * (std::size_t) (t + j)] = l[j];
                out[2 * (std::size_t) (t + j) + 1] = r[j];
            }
        }
        t += m;
    }
    return out;
}
} // namespace fruitkick
} // namespace lynx
