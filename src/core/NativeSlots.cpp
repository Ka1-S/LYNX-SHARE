#include "core/NativeSlots.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "core/ConvolverFx.h"
#include "core/DistructorFx.h"
#include "core/Effects.h"
#include "core/EquoFx.h"
#include "core/LovePhilter.h"
#include "core/MaximusFx.h"
#include "core/Vocodex.h"

namespace lynx::nativeslots
{
namespace
{
struct Bytes
{
    std::vector<std::uint8_t> b;
    template <typename T>
    void put (T v)
    {
        const auto* p = (const std::uint8_t*) &v;
        b.insert (b.end(), p, p + sizeof (v));
    }
    void i (int v) { put ((std::int32_t) v); }
    void f (double v) { put ((float) v); }
    void u8 (int v) { b.push_back ((std::uint8_t) v); }
    struct Point
    {
        double dx, y;
        float tension = 0.0f;
        int mode = 0;
    };
    // FL's envelope record: kind, version 3, points, flags / on / decay / loop / sustain
    void curve (int kind, const std::vector<Point>& points, bool on = true)
    {
        i (kind), i (3), i ((int) points.size());
        for (const auto& p : points)
            put (p.dx), put (p.y), f (p.tension), u8 (p.mode), u8 (0), u8 (0), u8 (0);
        i (0), i (on ? -1 : 0), i (-1), i (-1), i (-1);
    }
    void flat (double y, bool on = true) { curve (3, { { 0.0, y }, { 1.0, y } }, on); }
};

// Maximus (REA_rodada8 §1.2, REA_rodada40): version 7, the parameters at the plugin's reset values with the three bands
// doing nothing, the fixed block's editor bytes 0, the eight curves the identity (no compression)
std::vector<std::uint8_t> maximusState()
{
    Bytes s;
    s.i (7);
    for (const auto& p : maximusfx::stateParams())
        s.i (p.defaultValue);
    s.b.resize (4 + 0x1fa, 0);
    for (int k = 0; k < 8; ++k)
        s.curve (3, { { 0.0, 0.0 }, { 1.0, 1.0 } });
    return s.b;
}

// EQUO (REA_rodada61): version 4, every band on at gain 1 (the middle of 0…2), centred, no send
std::vector<std::uint8_t> equoState()
{
    const equofx::State d;
    Bytes s;
    s.i (4);
    for (int b = 0; b < equofx::numBands; ++b)
        for (int t = 0; t < equofx::numTargets; ++t)
            s.f (d.target[(std::size_t) b][(std::size_t) t].value), s.i (d.target[(std::size_t) b][(std::size_t) t].mode);
    for (int bank = 0; bank < equofx::numBanks; ++bank)
        for (int b = 0; b < equofx::numBands; ++b)
            for (int t = 0; t < equofx::numTargets; ++t)
                s.f (t == 2 ? 0.0 : 1.0); // volume and pan in the middle, nothing sent
    for (const auto k : d.knob)
        s.i (k);
    s.i (0), s.i (d.bandwidth), s.i (-1);
    s.b.resize (4 + (std::size_t) (equofx::numBands * equofx::numTargets) * 8 + 0xD14 + 0x14 + 0x17, 0);
    return s.b;
}

// Vocodex (REA_rodada35 §2.1): version 8, the constructor's maps (vocodex::defaultMap), 24 bands, no Soundgoodizer stage,
// the modulator on the insert's first side input, the carrier on the insert itself
std::vector<std::uint8_t> vocodexState()
{
    Bytes s;
    s.put ((std::uint32_t) 0x00080000u);
    for (int m = 0; m < 13; ++m)
    {
        std::vector<Bytes::Point> points;
        double last = 0.0;
        for (const auto& p : vocodex::defaultMap (m))
        {
            points.push_back ({ p.x - last, p.y, p.tension, (int) p.mode });
            last = p.x;
        }
        s.curve (3, points);
    }
    // wet, SG, modulator pass and its high-pass, carrier pass and its low-pass, noise and its high-pass, hold, attack, its
    // curve, release, its curve, freeze, band width, flatness, modulator width, modulator pitch, unison pitch and pan
    for (const int v : { 500, 0, 0, 0, 0, 65536, 0, 0, 1000, 1000, 1000, 3000, 1000, 0, 500, 0, 2000, 0, 0, 0 })
        s.i (v);
    s.i (0), s.i (2), s.i (0), s.i (24), s.i (1), s.i (1), s.i (0); // peak, order, unison, bands, not draft, inputs
    for (const int v : { 0, 1, 0, 1, 1, 0, 0, 0 })
        s.u8 (v);
    for (int m = 0; m < 13; ++m)
        s.u8 (m == 3 || m == 4 ? 1 : 0);
    s.u8 (0); // no internal carrier tone
    s.i (0);  // no notes
    return s.b;
}

// Love Philter (REA_rodada17 / core/LovePhilter.h): one low-pass unit playing, seven off; every envelope, LFO and map at rest
std::vector<std::uint8_t> lovePhilterState()
{
    philter::Doc d;
    d.versionWord = 0x000c0003u, d.version = 12;
    Bytes h;
    for (const int v : { 128, 128, 128, 64, 64, 1 }) // level, LFO, follower level, attack, release, pattern
        h.i (v);
    for (const int v : { 256, 256, 0, 0, -1, 0, 0 }) // Mod X, Mod Y, –, –, no solo, HQ envelopes off, no oversampling
        h.i (v);
    for (const int v : { 1, 0, 1, 0, 0 }) // smooth, centre, sync patterns
        h.u8 (v);
    h.u8 (0), h.i (0); // USE1
    h.u8 (0);          // no name
    d.head = h.b;
    d.tail = { 0 };
    const auto env = [] (int kind, double y, bool on) {
        philter::DocEnv e;
        e.kind = kind;
        e.pts.resize (2);
        e.pts[0].dt = 0.0, e.pts[0].y = y;
        e.pts[1].dt = 1.0, e.pts[1].y = y;
        e.on = on ? -1 : 0;
        e.sus = kind == 1 ? 1 : -1;
        return e;
    };
    for (std::size_t k = 0; k < d.u.size(); ++k)
    {
        auto& u = d.u[k];
        u.p = { 128, 190, 40, 128, 0, 0, 128, 0, 0, 128, k == 0 ? 128 : 0, 0 };
        u.mode = 2, u.type = 3; // two stages of the low-pass
        u.enabled = k == 0 ? 1 : 0;
        u.ws = env (3, 0.0, false);
        u.ws.pts[1].y = 1.0;
        for (std::size_t t = 0; t < u.art.size(); ++t)
        {
            auto& a = u.art[t];
            const auto centred = t == 0 || t == 2 || t == 3;
            const double rest = centred ? 0.5 : 1.0;
            a.base = centred ? 0.0f : 1.0f;
            for (auto& p : a.pat)
                p = env (1, rest, false);
            for (auto& knobs : a.patKnobs)
            {
                const std::int32_t adsr[4] { 128, 128, 0, 128 };
                std::memcpy (knobs.data(), adsr, 16);
            }
            a.lfo = env (2, 1.0, false);
            a.lfoKnobs = { 32768, 0, 0, 0, 0 };
            for (auto& m : a.maps)
                m = env (3, rest, false);
        }
    }
    return philter::writeDoc (d);
}

// LYNX's own room: a stereo impulse of 1.4 s, decorrelated noise under an exponential decay that is faster for the highs
// (the usual diffuse-field model), after a short gap
std::vector<std::uint8_t> roomWav()
{
    constexpr int rate = 44100, n = (int) (1.4 * rate);
    std::vector<float> x[2];
    for (int c = 0; c < 2; ++c)
    {
        x[c].assign ((std::size_t) n, 0.0f);
        std::uint32_t seed = c == 0 ? 0x1357911u : 0x2468bdfu;
        double low = 0.0;
        for (int i = 220; i < n; ++i)
        {
            seed = seed * 1664525u + 1013904223u;
            const auto white = (double) (std::int32_t) seed / 2147483648.0;
            const auto t = (double) (i - 220) / rate;
            low += 0.25 * (white - low); // (highs: the rest)
            x[c][(std::size_t) i] = (float) (0.16 * (1.6 * low * std::exp (-t * 6.908 / 1.3) + (white - low) * std::exp (-t * 6.908 / 0.6)));
        }
        x[c][0] = 0.0f;
    }
    Bytes w;
    const auto tag = [&w] (const char* t) { w.b.insert (w.b.end(), t, t + 4); };
    tag ("RIFF"), w.i (36 + n * 4), tag ("WAVE"), tag ("fmt "), w.i (16), w.put ((std::uint16_t) 1), w.put ((std::uint16_t) 2);
    w.i (rate), w.i (rate * 4), w.put ((std::uint16_t) 4), w.put ((std::uint16_t) 16), tag ("data"), w.i (n * 4);
    for (int i = 0; i < n; ++i)
        for (auto& c : x)
            w.put ((std::int16_t) std::lround (std::clamp (c[(std::size_t) i], -1.0f, 1.0f) * 32767.0f));
    return w.b;
}

// Fruity Convolver (REA_rodada60): version 15, the impulse embedded in the state, every envelope at rest, a flat EQ curve
std::vector<std::uint8_t> convolverState()
{
    const auto wav = roomWav();
    Bytes s;
    s.i (15);
    s.i (1000), s.i (400), s.i (0), s.i (0);
    s.u8 (0), s.u8 (0); // embedded, no path
    s.i ((int) wav.size());
    s.b.insert (s.b.end(), wav.begin(), wav.end());
    s.b.resize (s.b.size() + 0x24, 0);
    for (int e = 0; e < 5; ++e)
        s.flat (0.5);
    for (int k = 0; k < 4; ++k)
        s.u8 (0);
    s.i (0);
    for (const int v : { 0, 0, 0, 0, 1000 })
        s.i (v);
    return s.b;
}

std::vector<std::uint8_t> distructorState()
{
    distructor::State s;
    using distructor::ModuleType;
    s.modules = { distructor::defaultModule (ModuleType::distortion), distructor::defaultModule (ModuleType::filter),
                  distructor::defaultModule (ModuleType::chorus), distructor::defaultModule (ModuleType::distortion) }; // (an amp / cabinet needs FL's impulses)
    return distructor::serialize (s);
}
} // namespace

std::optional<EffectSlot> make (EffectKind kind)
{
    auto slot = makeEffectSlot (kind);
    switch (kind)
    {
        case EffectKind::maximus:
        {
            maximusfx::Blob blob;
            blob.state = maximusState();
            if (maximusfx::presetOf (blob) == nullptr)
                return std::nullopt;
            slot.state = maximusfx::pack (blob);
            return slot;
        }
        case EffectKind::equo:
        {
            slot.state = equoState();
            const auto s = equofx::parse (slot.state);
            if (! s)
                return std::nullopt;
            equofx::toParams (*s, slot.params);
            return slot;
        }
        case EffectKind::vocodex:
        {
            slot.state = vocodexState();
            const auto s = vocodex::parseState (slot.state);
            if (! s)
                return std::nullopt;
            vocodex::controlsToParams (*s, slot.params);
            return slot;
        }
        case EffectKind::lovePhilter: return philter::slotFromFl (lovePhilterState());
        case EffectKind::convolver: return convolverFromFl (convolverState(), {});
        case EffectKind::distructor:
        {
            const auto state = distructorState();
            const auto s = distructor::parse (state);
            if (! s)
                return std::nullopt;
            slot.state = distructor::packBlob (state, {});
            distructor::toParams (*s, slot.params.data());
            return slot;
        }
        default: return std::nullopt;
    }
}
} // namespace lynx::nativeslots
