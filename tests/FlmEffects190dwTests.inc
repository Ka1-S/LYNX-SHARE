// --- FL Studio Mobile (.flm) import, round 190dw: the seven effect modules that were approximated (Equalizer, Graphic
// EQ, Filter, Autoduck legacy, Reverb legacy, Reverb 2, Reverb) now play on FLM's own engines (Pocket EQ / Graphic EQ /
// Filter / Reverb Classic / Reverb 2 / Reverb, and Pocket Autoduck for the legacy Autoduck). Each check below runs
// LYNX's import of a module against what FL Studio Mobile itself produced for the same module inside FL (the numbers in
// FlmEffects190dwData.inc: impulse responses of our own click stimulus and the gain curve on our own 1 kHz staircase,
// rendered by FL's command line; settings mostly taken from FL's FLM sample songs). Nothing of Image-Line's is
// committed: only the measured numbers.

#include <complex>

#include "core/FlmEffects.h"
#include "core/PocketFx.h"

namespace fx190dw
{
struct LinearRef
{
    const char* name;
    int type;
    std::vector<float> params; // the module's PRMS (index 0 = Enable)
    int lagL, lagR;            // first output sample above 1e-3 of the peak, after the click
    std::vector<float> dbL, dbR; // |H| in dB at 50, 100, 200, 500 Hz, 1, 2, 4, 8, 12, 16 kHz
};
struct ReverbRef
{
    const char* name;
    int type;
    std::vector<float> params;
    // per octave band 125 Hz … 8 kHz of L + R after a click on the left (its dry first sample removed): the energy (dB)
    // in 1–100 ms, 100–300 ms and 300–700 ms after the click
    std::vector<float> w1, w2, w3;
};
struct DuckRef
{
    const char* name;
    std::vector<float> params;
    std::vector<float> gainDb; // 32 phases of a beat at 120 BPM: 1 ms RMS of out / in, the median of beats 2…6
};

constexpr double rate = 44100.0;
constexpr float click = 0.194793701f; // the click as FL's render test plays it (our 0.5 at −8.2 dB)
const std::array<double, 10> freqs { 50, 100, 200, 500, 1000, 2000, 4000, 8000, 12000, 16000 };

inline lynx::flm::Module moduleOf (int type, const std::vector<float>& params)
{
    lynx::flm::Module m;
    m.type = (std::uint32_t) type;
    m.instanceId = 2;
    m.params = params;
    return m;
}

// the import's slots on a stereo buffer as FL hosts FLM: one 512-sample block of silence first, 512-sample blocks,
// 120 BPM, the song position from 0
inline void run (const std::vector<lynx::EffectSlot>& slots, std::vector<float>& l, std::vector<float>& r)
{
    using namespace lynx;
    for (const auto& slot : slots)
    {
        if (! slot.enabled)
            continue;
        auto fx = makeEffect (slot);
        fx->prepare (rate);
        fx->setPlaying (true);
        std::vector<float> zl (512, 0.0f), zr (512, 0.0f);
        fx->process (zl.data(), zr.data(), 512, slot.params, 120.0);
        const auto dl = l, dr = r;
        for (std::size_t at = 0; at < l.size(); at += 512)
        {
            fx->setSongPosition ((double) at * 2.0 * (double) ppq / rate);
            fx->process (l.data() + at, r.data() + at, (int) std::min<std::size_t> (512, l.size() - at), slot.params, 120.0);
        }
        if (slot.mix < 1.0f)
            for (std::size_t i = 0; i < l.size(); ++i)
                l[i] = slot.mix * l[i] + (1.0f - slot.mix) * dl[i], r[i] = slot.mix * r[i] + (1.0f - slot.mix) * dr[i];
    }
}

inline double dbAt (const std::vector<float>& h, std::size_t start, std::size_t n, double hz)
{
    std::complex<double> s = 0.0;
    const auto w = -2.0 * 3.141592653589793 * hz / rate;
    for (std::size_t i = 0; i < n && start + i < h.size(); ++i)
        s += (double) h[start + i] / (double) click * std::polar (1.0, w * (double) i);
    return 20.0 * std::log10 (std::abs (s) + 1e-12);
}

inline int firstAbove (const std::vector<float>& h, std::size_t start)
{
    float peak = 0.0f;
    for (std::size_t i = start; i < h.size(); ++i)
        peak = std::max (peak, std::abs (h[i]));
    for (std::size_t i = start; i < h.size(); ++i)
        if (std::abs (h[i]) > peak * 1e-3f)
            return (int) (i - start);
    return -1;
}

// FL's render is a 16-bit file: LYNX's output is rounded and clipped the same way before it is compared
inline void as16bit (std::vector<float>& x)
{
    for (auto& v : x)
        v = (float) (std::clamp (std::nearbyint ((double) v * 32768.0), -32768.0, 32767.0) / 32768.0);
}

inline void fft (std::vector<std::complex<double>>& a, bool inverse)
{
    const auto n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i)
    {
        auto bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap (a[i], a[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1)
    {
        const auto ang = 2.0 * 3.141592653589793 / (double) len * (inverse ? 1.0 : -1.0);
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (std::size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0);
            for (std::size_t j = 0; j < len / 2; ++j)
            {
                const auto u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wl;
            }
        }
    }
    if (inverse)
        for (auto& x : a)
            x /= (double) n;
}

// per octave band (125 Hz … 8 kHz): the energy (dB) in three windows after the click (brick-wall bands on 2^18 samples)
inline std::vector<std::array<double, 3>> bandWindows (const std::vector<double>& x)
{
    const std::size_t n = x.size();
    std::vector<std::complex<double>> spec (n);
    for (std::size_t i = 0; i < n; ++i)
        spec[i] = x[i];
    fft (spec, false);
    std::vector<std::array<double, 3>> out;
    for (const double fc : { 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0 })
    {
        const auto lo = fc / std::sqrt (2.0), hi = fc * std::sqrt (2.0);
        auto b = spec;
        for (std::size_t k = 0; k <= n / 2; ++k)
        {
            const auto f = (double) k * rate / (double) n;
            if (f < lo || f >= hi)
            {
                b[k] = 0.0;
                if (k != 0 && k != n / 2)
                    b[n - k] = 0.0;
            }
        }
        fft (b, true);
        std::array<double, 3> e {};
        const double windows[3][2] { { 0.001, 0.1 }, { 0.1, 0.3 }, { 0.3, 0.7 } };
        for (int w = 0; w < 3; ++w)
        {
            double sum = 0.0;
            for (auto i = (std::size_t) (windows[w][0] * rate); i < (std::size_t) (windows[w][1] * rate); ++i)
                sum += b[i].real() * b[i].real();
            e[(std::size_t) w] = 10.0 * std::log10 (sum + 1e-30);
        }
        out.push_back (e);
    }
    return out;
}
} // namespace fx190dw

#include "FlmEffects190dwData.inc"

TEST (flm190dw_effects_on_flm_engines)
{
    using namespace lynx;
    // every module of the seven types maps to one exact engine slot, nothing listed as approximated
    for (const int type : { 3, 7, 9, 10, 12, 32, 35 })
    {
        const auto m = flmfx::map (fx190dw::moduleOf (type, { 1.0f }));
        CHECK (m.slots.size() == 1 && m.partial.empty());
    }
    CHECK (flmfx::map (fx190dw::moduleOf (3, { 1.0f })).slots[0].kind == EffectKind::pocketEq);
    CHECK (flmfx::map (fx190dw::moduleOf (7, { 1.0f })).slots[0].kind == EffectKind::pocketGraphicEq);
    CHECK (flmfx::map (fx190dw::moduleOf (9, { 1.0f })).slots[0].kind == EffectKind::pocketFilter);
    CHECK (flmfx::map (fx190dw::moduleOf (10, { 1.0f })).slots[0].kind == EffectKind::pocketAutoduck);
    CHECK (flmfx::map (fx190dw::moduleOf (12, { 1.0f })).slots[0].kind == EffectKind::pocketReverbClassic);
    CHECK (flmfx::map (fx190dw::moduleOf (35, { 1.0f })).slots[0].kind == EffectKind::pocketReverb2);
    CHECK (flmfx::map (fx190dw::moduleOf (32, { 1.0f })).slots[0].kind == EffectKind::pocketReverb);
    // the old LYNX slots these modules became before (saved in projects) are left alone: their engines are unchanged
    CHECK (std::string (effectInfo (EffectKind::reeverb2).name) == "Reeverb 2" && std::string (effectInfo (EffectKind::rhythmGain).name) != "");
}

TEST (flm190dw_eq_filter_against_flm_renders)
{
    using namespace lynx;
    for (const auto& ref : linearRefs)
    {
        const auto m = flmfx::map (fx190dw::moduleOf (ref.type, ref.params));
        CHECK (m.slots.size() == 1);
        const std::size_t at = 22050, n = 1 << 15;
        for (int ch = 0; ch < 2; ++ch)
        {
            std::vector<float> l (at + n + 1024, 0.0f), r (l.size(), 0.0f);
            (ch == 0 ? l : r)[at] = fx190dw::click;
            fx190dw::run (m.slots, l, r);
            fx190dw::as16bit (l);
            fx190dw::as16bit (r);
            const auto& y = ch == 0 ? l : r;
            const auto& want = ch == 0 ? ref.dbL : ref.dbR;
            const int lag = fx190dw::firstAbove (y, at);
            CHECK (lag == (ch == 0 ? ref.lagL : ref.lagR));
            double worst = 0.0;
            for (std::size_t k = 0; k < fx190dw::freqs.size(); ++k)
            {
                if (want[k] < -50.0f) // near the 16-bit render's floor (the click is −14 dBFS)
                    continue;
                const auto got = fx190dw::dbAt (y, at, n, fx190dw::freqs[k]);
                // within 0.05 dB where the response is above −20 dB; on a steep skirt (−20…−50 dB) float rounding of a
                // 48 dB/oct design (an eight-biquad band-pass most) moves it by up to 0.18 dB: 0.25 dB there
                worst = std::max (worst, std::abs (got - (double) want[k]) / (want[k] > -20.0f ? 0.05 : 0.25));
            }
            CHECK (worst < 1.0);
            if (worst >= 1.0 || lag != (ch == 0 ? ref.lagL : ref.lagR))
                std::printf ("  %s channel %d: lag %d (FLM %d), worst %.2f of the tolerance\n", ref.name, ch, lag, ch == 0 ? ref.lagL : ref.lagR, worst);
        }
    }
}

TEST (flm190dw_reverbs_against_flm_renders)
{
    using namespace lynx;
    for (const auto& ref : reverbRefs)
    {
        const auto m = flmfx::map (fx190dw::moduleOf (ref.type, ref.params));
        CHECK (m.slots.size() == 1);
        const std::size_t at = 22050, n = 1 << 18;
        std::vector<float> l (at + n, 0.0f), r (l.size(), 0.0f);
        l[at] = fx190dw::click;
        fx190dw::run (m.slots, l, r);
        fx190dw::as16bit (l);
        fx190dw::as16bit (r);
        std::vector<double> sum (n);
        for (std::size_t i = 0; i < n; ++i)
            sum[i] = ((double) l[at + i] + (double) r[at + i]) / (double) fx190dw::click;
        sum[0] = 0.0; // the dry click (the wet starts later in every module)
        const auto bands = fx190dw::bandWindows (sum);
        double worst = 0.0;
        for (std::size_t b = 0; b < bands.size(); ++b)
            for (int w = 0; w < 3; ++w)
            {
                const auto want = (double) (w == 0 ? ref.w1 : w == 1 ? ref.w2 : ref.w3)[b];
                if (want > -75.0) // above the 16-bit render's floor
                    worst = std::max (worst, std::abs (bands[b][(std::size_t) w] - want));
            }
        CHECK (worst < 0.2);
        if (worst >= 0.2)
            std::printf ("  %s: band energy off by %.3f dB\n", ref.name, worst);
    }
}

TEST (flm190dw_autoduck_against_flm_render)
{
    using namespace lynx;
    for (const auto& ref : duckRefs)
    {
        const auto m = flmfx::map (fx190dw::moduleOf (10, ref.params));
        CHECK (m.slots.size() == 1);
        const std::size_t n = (std::size_t) (3.5 * fx190dw::rate);
        std::vector<float> l (n), r (n);
        for (std::size_t i = 0; i < n; ++i)
            l[i] = r[i] = 0.5f * (float) std::sin (2.0 * 3.141592653589793 * 1000.0 * (double) i / fx190dw::rate);
        const auto in = l;
        fx190dw::run (m.slots, l, r);
        const std::size_t beat = 22050, frame = 44;
        double worst = 0.0;
        for (int ph = 0; ph < 32; ++ph)
        {
            std::vector<double> g;
            for (const int b : { 2, 3, 4, 5, 6 })
            {
                const auto a = (std::size_t) b * beat + (std::size_t) (ph * (int) beat / 32);
                double sy = 0.0, sd = 0.0;
                for (std::size_t i = a; i < a + frame; ++i)
                    sy += (double) l[i] * l[i], sd += (double) in[i] * in[i];
                g.push_back (10.0 * std::log10 (sy / sd + 1e-30));
            }
            std::sort (g.begin(), g.end());
            const auto want = (double) ref.gainDb[(std::size_t) ph];
            worst = std::max (worst, std::abs (g[2] - want) * (want < -30.0 ? 0.2 : 1.0));
        }
        CHECK (worst < 0.1);
        if (worst >= 0.1)
            std::printf ("  %s: gain curve off by %.3f dB\n", ref.name, worst);
    }
}

// FL's own FLM sample songs, when present (LYNX_FLM_DIR, as flm182c_real_projects): every module of the seven types
// imports onto its exact engine and none is listed as approximated
TEST (flm190dw_sample_songs_effects)
{
    using namespace lynx;
    const char* dir = std::getenv ("LYNX_FLM_DIR");
    if (dir == nullptr)
        return;
    int modules = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator (dir))
    {
        const auto ext = entry.path().extension().string();
        if (ext != ".flm" && ext != ".songtmpl")
            continue;
        std::ifstream in (entry.path(), std::ios::binary);
        const std::vector<std::uint8_t> bytes ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
        const auto project = flm::parse (std::span<const std::uint8_t> (bytes));
        if (! project)
            continue;
        for (const auto& rack : project->racks)
            for (const auto& m : rack.modules)
                if (m.type == 3 || m.type == 7 || m.type == 9 || m.type == 10 || m.type == 12 || m.type == 32 || m.type == 35)
                {
                    ++modules;
                    const auto mapped = flmfx::map (m);
                    // one engine slot (and the utility of a module's Post Gain, the wrapper's exact law), nothing approximated
                    const auto ok = mapped.partial.empty() && ! mapped.slots.empty()
                                    && (mapped.slots.size() == 1 || (mapped.slots.size() == 2 && mapped.slots[1].kind == EffectKind::utility));
                    CHECK (ok);
                    if (! ok)
                        std::printf ("  %s: module %u: %zu slots, %s\n", entry.path().filename().string().c_str(), m.type, mapped.slots.size(),
                                     mapped.partial.empty() ? "" : mapped.partial[0].c_str());
                }
    }
    std::printf ("  %d EQ / filter / autoduck / reverb modules in the sample songs\n", modules);
}
