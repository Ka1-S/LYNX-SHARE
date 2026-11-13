// FL Studio's Plucked! (REA_rodada65 §1). See Plucked.h.

#include "core/Plucked.h"
#include "core/DrumParamRanges.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>

#include "core/FlVoiceProcessor.h" // (FL's x87 host pan law, round-half-even)

#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize ("fp-contract=off")
#endif

namespace lynx
{
namespace
{
using flvp::roundEven;

struct Params
{
    int decay = 128, colour = 64, normalize = 1, gate = 0, widen = 1;
};

bool parse (const std::vector<std::uint8_t>& s, Params& p)
{
    if (s.size() < 20)
        return false;
    std::int32_t v[5];
    std::memcpy (v, s.data(), 20);
    p.decay = v[0], p.colour = v[1], p.normalize = v[2], p.gate = v[3], p.widen = v[4];
    return true;
}

// Delphi's Random: RandSeed = RandSeed·0x08088405 + 1, value = RandSeed·2^-32 (§1.4)
struct DelphiRandom
{
    std::uint32_t seed = 0;
    double next() noexcept
    {
        seed = seed * 0x08088405u + 1u;
        return (double) seed * 2.3283064365386963e-10;
    }
};

// Delphi's Power(Single, Single) as the plugin calls it (§1.3): integral exponents through IntPower
float delphiPower (float base, float e) noexcept
{
    if (e == 0.0f)
        return 1.0f;
    if (base == 0.0f && e > 0.0f)
        return 0.0f;
    if ((float) std::trunc ((double) e + 0.5) == e)
    {
        const auto n = (int) e;
        double r = 1.0;
        for (int i = 0; i < std::abs (n); ++i)
            r *= (double) base;
        return (float) (n < 0 ? 1.0 / r : r);
    }
    return (float) std::exp ((double) e * std::log ((double) base));
}

// the pitch ratio (relative to 523.2511306 Hz) of a note's Final.Pitch in cents (§1.3)
float pitchRatio (float pitchCents) noexcept
{
    auto c = roundEven ((double) pitchCents);
    if (c < -3600)
        c = (c + 100) % 1200 - 2500;
    else if (c >= 3600)
        c = c % 1200 + 2400;
    const auto e = (float) (c - 2400) * 0.0008333333535119891f + 1.0f;
    return delphiPower (2.0f, e);
}

// the per-sample feedback gain (§1.5): decay knob + Mod X, the log law, ×0.5 (the two-point average)
float feedbackGain (const Params& P, float modX, bool normalize, float invRatio) noexcept
{
    const auto lv = std::clamp ((std::int64_t) P.decay + roundEven ((double) (modX * 256.0f)), (std::int64_t) 0, (std::int64_t) 256);
    const auto d = std::log ((double) ((float) lv + 1.0f)) / std::log (257.0);
    auto g = (float) ((double) ((float) d * 0.04989999905228615f) + 0.95);
    if (normalize)
        g = delphiPower (g, invRatio);
    return g * 0.5f;
}

constexpr int maxVoices = 64;

struct Voice
{
    bool active = false, released = false, wide = false, normalize = false;
    int key = 0, N = 0, pos = 0, counter = -1;
    float g = 0.0f, invRatio = 0.0f, modX = 0.0f, prevL = 0.0f, prevR = 0.0f;
    float peak = 0.0f, lastPeak = 1.0f;
    float fvol = 1.0f, fpan = 0.0f, gainL = 0.0f, gainR = 0.0f;
    std::uint64_t order = 0;
    float* line = nullptr; // N (mono) or 2N (widen: left line, then right line) samples
};

class Engine
{
public:
    Params P;
    DelphiRandom random;
    bool panLawA = true, ramp = true;
    int maxPoly = 0;
    std::array<Voice, maxVoices> voices {};

    void prepare (double sampleRate)
    {
        rate = sampleRate;
        // the longest line: c = −3600 → ratio 2^-4 (32.7 Hz)
        capacity = (int) std::ceil (sampleRate / (523.251130601197 / 16.0)) + 8;
        storage.assign ((std::size_t) maxVoices * (std::size_t) capacity * 2, 0.0f);
        for (int i = 0; i < maxVoices; ++i)
            voices[(std::size_t) i] = Voice {}, voices[(std::size_t) i].line = storage.data() + (std::size_t) i * (std::size_t) capacity * 2;
        scratch.assign (1024, 0.0f);
    }

    bool idle() const noexcept
    {
        return std::none_of (voices.begin(), voices.end(), [] (const Voice& v) { return v.active; });
    }

    void releaseAll() noexcept
    {
        for (auto& v : voices)
            if (v.active)
                release (v);
    }

    void release (Voice& v) noexcept
    {
        if (P.gate > 0)
            v.released = true;
    }

    // TriggerVoice (§1.2–§1.4)
    Voice& trigger (int key, float finalPitch, float fvol, float fpan, float modX, float modY) noexcept
    {
        // max polyphony: the quietest sounding voice (smallest last peak) is released
        if (maxPoly > 0)
        {
            int sounding = 0;
            Voice* quietest = nullptr;
            float least = 2.0f;
            for (auto* v : ordered())
                if (! v->released)
                {
                    ++sounding;
                    if (v->lastPeak < least)
                        least = v->lastPeak, quietest = v;
                }
            if (sounding >= maxPoly && quietest != nullptr)
                quietest->released = true;
        }
        Voice* slot = nullptr;
        for (auto& v : voices)
            if (! v.active)
            {
                slot = &v;
                break;
            }
        if (slot == nullptr) // (pool full: the oldest voice makes room)
            slot = ordered().front();
        auto& v = *slot;
        auto* line = v.line;
        v = Voice {};
        v.line = line;
        v.active = true, v.key = key, v.order = ++serial;
        v.fvol = fvol, v.fpan = fpan, v.modX = modX;

        const auto ratio = pitchRatio (finalPitch);
        v.N = std::clamp ((int) roundEven (rate / ((double) ratio * 523.251130601197)), 1, capacity);
        v.normalize = P.normalize > 0;
        if (v.normalize)
            v.invRatio = 1.0f / ratio;
        v.wide = P.widen > 0;
        const auto lv = std::clamp ((std::int64_t) P.colour + roundEven ((double) (modY * 128.0f)), (std::int64_t) 0, (std::int64_t) 128);
        const auto c = (float) lv * 0.0078125f;
        const auto a = (1.0f - c) * 0.5f;
        const auto half = v.N >> 1;
        for (int i = 0; i < v.N; ++i)
            line[i] = i < half ? a : -a;
        if (v.wide)
            for (int i = 0; i < v.N; ++i)
                line[v.N + i] = (float) ((double) line[i] + (random.next() * 2.0 - 1.0) * (double) c);
        for (int i = 0; i < v.N; ++i)
            line[i] = (float) ((double) line[i] + (random.next() * 2.0 - 1.0) * (double) c);
        v.g = feedbackGain (P, modX, v.normalize, v.invRatio);
        return v;
    }

    // one host block of every voice, added into left/right (§1.5, §1.6)
    void render (float* left, float* right, int n) noexcept
    {
        if ((int) scratch.size() < 2 * n)
            n = (int) scratch.size() / 2; // (callers keep blocks ≤ 512)
        for (auto& v : voices)
        {
            if (! v.active)
                continue;
            if (decayDirty)
                v.g = feedbackGain (P, v.modX, v.normalize, v.invRatio);
            float tl = 0.0f, tr = 0.0f;
            if (! v.released)
                gains (v.fpan, v.fvol, tl, tr);
            if (v.counter < 0)
                v.gainL = tl, v.gainR = tr, v.counter = 0;
            const auto g = v.g;
            auto* L = v.line;
            auto* R = v.line + v.N;
            float pL = v.prevL, pR = v.prevR;
            int p = v.pos;
            float peakL = 0.0f, peakR = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                const auto y = L[p];
                L[p] = g * (y + pL);
                pL = y;
                scratch[(std::size_t) (2 * i)] = y;
                peakL = std::max (peakL, std::fabs (y));
                if (v.wide)
                {
                    const auto yr = R[p];
                    R[p] = g * (yr + pR);
                    pR = yr;
                    scratch[(std::size_t) (2 * i + 1)] = yr;
                    peakR = std::max (peakR, std::fabs (yr));
                }
                else
                    scratch[(std::size_t) (2 * i + 1)] = y;
                if (++p >= v.N)
                    p = 0;
            }
            v.prevL = pL, v.prevR = pR, v.pos = p;
            // the host adds the voice with its L/R gains, ramping from the previous block's
            const auto sl = v.gainL, sr = v.gainR;
            const bool moving = ramp && (sl != tl || sr != tr);
            for (int i = 0; i < n; ++i)
            {
                const auto t = moving ? (float) (i + 1) / (float) n : 1.0f;
                const auto gl = moving ? sl + (tl - sl) * t : tl;
                const auto gr = moving ? sr + (tr - sr) * t : tr;
                left[i] += scratch[(std::size_t) (2 * i)] * gl;
                right[i] += scratch[(std::size_t) (2 * i + 1)] * gr;
            }
            v.gainL = tl, v.gainR = tr;
            if (! v.released)
            {
                // level watch: every stretch of more than 256 samples, a peak below 0.004 ends the voice
                v.peak = std::max (v.peak, std::max (peakL, peakR));
                v.counter += n;
                if (v.counter > 256)
                {
                    v.counter = 0;
                    v.lastPeak = v.peak;
                    if (v.peak >= 0.004f)
                        v.peak = 0.0f;
                    else
                        v.active = false;
                }
            }
            else if (tl == 0.0f && tr == 0.0f)
                v.active = false;
        }
        decayDirty = false;
    }

    void setFinal (Voice& v, float fvol, float fpan) noexcept { v.fvol = fvol, v.fpan = fpan; }
    void markDecayDirty() noexcept { decayDirty = true; }

private:
    void gains (float pan, float vol, float& l, float& r) const noexcept
    {
        if (panLawA)
            flvp::panLaw (pan, vol, l, r);
        else
            l = r = vol;
    }

    // the active voices, oldest first (FL keeps them in trigger order); no allocation
    struct Ordered
    {
        std::array<Voice*, maxVoices> v {};
        int count = 0;
        Voice** begin() noexcept { return v.data(); }
        Voice** end() noexcept { return v.data() + count; }
        Voice* front() const noexcept { return v[0]; }
    };
    Ordered ordered() noexcept
    {
        Ordered out;
        for (auto& v : voices)
            if (v.active)
                out.v[(std::size_t) out.count++] = &v;
        std::sort (out.begin(), out.end(), [] (const Voice* a, const Voice* b) { return a->order < b->order; });
        return out;
    }

    double rate = 44100.0;
    int capacity = 0;
    std::uint64_t serial = 0;
    bool decayDirty = false;
    std::vector<float> storage, scratch;
};

// ------------------------------------------------------------------------------------------------------- LYNX front-end
//
// Levels as the other FL voice generators (REA_rodada47 §2.1): Final.Vol = channel volume × Init.Vol (Init.Vol =
// (11^(1.28·vel) − 1)/10 from LYNX's velocity), Final.Pan = note pan + channel pan, Final.Pitch = 100·(key − 60) +
// channel pitch + note cents; LYNX's channel gains gl = √2·V·cos((1 + p)π/4), gr = √2·V·sin(…) give V and p back.

class PluckedInstrument final : public Instrument
{
public:
    PluckedInstrument() { engine.prepare (44100.0); }

    void prepare (double sr) noexcept override
    {
        engine.prepare (sr);
        engine.P = loadParams();
    }

    void configure (const ChannelDef& channel, double) noexcept override
    {
        pitchCents.store (channel.sampler.pitch * 100.0f);
        maxPoly.store (channel.maxPolyphony == 1 ? 1 : 0 /* FL tells the generator its Max polyphony, not the Mono box (round 168l) */);
        Params p;
        if (parse (channel.pluginState, p))
        {
            knobs[0].store (p.decay), knobs[1].store (p.colour), knobs[2].store (p.normalize);
            knobs[3].store (p.gate), knobs[4].store (p.widen);
        }
    }

    void setNextNoteExpression (float pan, float cents, bool) noexcept override
    {
        nextPan = pan;
        nextCents = cents;
    }

    // automation (round 168p): the clip's 0…1 over the knob's range (the engine reads the knobs every block)
    void setParameter (int index, float value) noexcept override
    {
        const auto r = drumranges::plucked (index);
        if (r.valid && std::isfinite (value))
            knobs[(std::size_t) index].store ((int) r.raw (value));
    }

    void noteOn (int key, float velocity) noexcept override
    {
        sync();
        const auto vel = std::clamp ((double) velocity, 0.0, 1.0);
        const auto initVol = (std::pow (11.0, 1.28 * vel) - 1.0) / 10.0;
        const auto k = std::clamp (key, 0, 127);
        const auto finalPitch = (float) (100.0 * (k - 60) + (double) pitchCents.load() + (double) nextCents);
        auto& v = engine.trigger (k, finalPitch, (float) (channelVol * initVol),
                                  (float) std::clamp ((double) nextPan + channelPan, -1.0, 1.0), 0.0f, 0.0f);
        const auto slot = (std::size_t) (&v - engine.voices.data());
        volFactor[slot] = (float) initVol;
        slotNote[slot] = startingNoteId(); // (round 190ej)
        notePan[slot] = nextPan;
        nextPan = 0.0f;
        nextCents = 0.0f;
    }

    void noteOff (int key) noexcept override
    {
        // (round 190ej: the note's own voice — FL hands Plucked! the ending note's voice, REA_rodada65 §1)
        for (auto& v : engine.voices)
            if (v.active && endsVoice (v.key, slotNote[(std::size_t) (&v - engine.voices.data())], key) && ! v.released)
                engine.release (v);
    }

    void allNotesOff() noexcept override { engine.releaseAll(); }

    bool idle() const noexcept override { return engine.idle(); }

    void render (float* left, float* right, int n, float gl, float gr) noexcept override
    {
        sync();
        const auto vol = std::sqrt (((double) gl * gl + (double) gr * gr) * 0.5);
        auto pan = vol > 0.0 ? std::atan2 ((double) gr, (double) gl) * (4.0 / 3.141592653589793) - 1.0 : 0.0;
        if (std::fabs (pan) < 1e-9)
            pan = 0.0;
        if (vol != channelVol || pan != channelPan)
        {
            channelVol = vol, channelPan = pan;
            for (std::size_t i = 0; i < engine.voices.size(); ++i)
                if (engine.voices[i].active)
                    engine.setFinal (engine.voices[i], (float) (vol * (double) volFactor[i]),
                                     (float) std::clamp ((double) notePan[i] + pan, -1.0, 1.0));
        }
        for (int done = 0; done < n;)
        {
            const auto len = std::min (512, n - done);
            engine.render (left + done, right + done, len);
            done += len;
        }
    }

private:
    void sync() noexcept
    {
        engine.maxPoly = maxPoly.load();
        const auto p = loadParams();
        if (p.decay != engine.P.decay)
            engine.markDecayDirty(); // (parameter 0 makes every voice recompute its feedback gain)
        engine.P = p;
    }

    Params loadParams() const noexcept
    {
        Params p;
        p.decay = knobs[0].load(), p.colour = knobs[1].load(), p.normalize = knobs[2].load();
        p.gate = knobs[3].load(), p.widen = knobs[4].load();
        return p;
    }

    Engine engine;
    std::array<std::atomic<int>, 5> knobs { 128, 64, 1, 0, 1 };
    std::atomic<float> pitchCents { 0.0f };
    std::atomic<int> maxPoly { 0 };
    std::array<float, maxVoices> volFactor {}, notePan {};
    std::array<std::uint32_t, maxVoices> slotNote {}; // each voice slot's LYNX note id (round 190ej)
    double channelVol = 1.0, channelPan = 0.0;
    float nextPan = 0.0f, nextCents = 0.0f;
};
} // namespace

bool pluckedStateUsable (const std::vector<std::uint8_t>& state)
{
    Params p;
    return parse (state, p);
}

std::optional<std::vector<int>> pluckedParams (const std::vector<std::uint8_t>& state)
{
    Params p;
    if (! parse (state, p))
        return std::nullopt;
    return std::vector<int> { p.decay, p.colour, p.normalize, p.gate, p.widen };
}

std::unique_ptr<Instrument> makePluckedInstrument() { return std::make_unique<PluckedInstrument>(); }

namespace plucked
{
std::vector<float> renderReference (const std::vector<std::uint8_t>& state, const std::vector<RefNote>& notes, double sampleRate,
                                    int length, int block, std::uint32_t seed, bool panLawA, int maxPoly)
{
    Params P;
    if (! parse (state, P) || length <= 0)
        return {};
    auto engine = std::make_unique<Engine>();
    engine->prepare (sampleRate);
    engine->P = P, engine->random.seed = seed, engine->panLawA = panLawA, engine->ramp = false, engine->maxPoly = maxPoly;
    std::vector<float> out ((std::size_t) length * 2, 0.0f), L ((std::size_t) block), R ((std::size_t) block);
    std::vector<Voice*> handle (notes.size(), nullptr);
    block = std::clamp (block, 1, 512);
    for (int n0 = 0; n0 < length;)
    {
        auto len = std::min (block, length - n0);
        for (const auto& e : notes)
        {
            if (e.start > n0 && e.start < n0 + len)
                len = e.start - n0;
            if (e.release > n0 && e.release < n0 + len)
                len = e.release - n0;
        }
        for (std::size_t i = 0; i < notes.size(); ++i)
        {
            const auto& e = notes[i];
            if (e.start == n0)
                handle[i] = &engine->trigger (e.key, (float) (100.0 * (e.key - 60) + e.pitchOffset), (float) e.finalVol,
                                              (float) e.finalPan, (float) e.modX, (float) e.modY);
            if (e.release == n0 && handle[i] != nullptr && handle[i]->active)
                engine->release (*handle[i]);
        }
        std::fill (L.begin(), L.end(), 0.0f);
        std::fill (R.begin(), R.end(), 0.0f);
        engine->render (L.data(), R.data(), len);
        for (int i = 0; i < len; ++i)
            out[(std::size_t) (2 * (n0 + i))] = L[(std::size_t) i], out[(std::size_t) (2 * (n0 + i) + 1)] = R[(std::size_t) i];
        n0 += len;
    }
    return out;
}
} // namespace plucked
} // namespace lynx
