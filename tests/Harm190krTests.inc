// Round beatfl190kr (0.1.30, perf-first): Harmless's sine-bank kernel as explicit SSE2 / AVX vectors with two groups
// of 8 partials per pass must give exactly what the one-group scalar loop of round 190kg's build gave. The reference
// below is that loop verbatim; both builds (SSE2, and AVX where the CPU has it) must match it bit for bit — the
// oscillator states and every lane sum — for every bank height, segment lengths around the control blocks, and
// partials whose amplitude and target are zero.

#include <limits>
#include <random>

namespace harm190kr
{
constexpr double pi = 3.14159265358979323846;

void bankKernelReference (double* osc0, double* osc1, const double* oscC, const double* T, const double* A, int top, int m,
                          const double* kp, double* acc) noexcept
{
    for (int g = 0; g < top; g += 8)
    {
        double x[8], y[8], c[8], t[8], d[8];
        for (int l = 0; l < 8; ++l)
        {
            const auto i = (std::size_t) (g + l);
            x[l] = osc0[i], y[l] = osc1[i], c[l] = oscC[i];
            t[l] = T[i];
            d[l] = A[i] - T[i];
        }
        for (int j = 0; j < m; ++j)
        {
            const auto k = kp[j];
            auto* a = acc + 8 * j;
            for (int l = 0; l < 8; ++l)
            {
                const auto s = x[l];
                a[l] += (t[l] + d[l] * k) * s;
                x[l] = c[l] * s - y[l];
                y[l] = s;
            }
        }
        for (int l = 0; l < 8; ++l)
            osc0[(std::size_t) (g + l)] = x[l], osc1[(std::size_t) (g + l)] = y[l];
    }
}

struct BankState
{
    std::vector<double> osc0, osc1, oscC, T, A, kp, acc;

    BankState (int m, unsigned seed)
    {
        std::mt19937_64 rng (seed);
        std::uniform_real_distribution<double> u (-1.0, 1.0);
        constexpr int size = 528;
        osc0.resize (size), osc1.resize (size), oscC.resize (size), T.resize (size), A.resize (size);
        for (int i = 0; i < size; ++i)
        {
            const auto f = 0.0013 * (i + 1) * (1.0 + 0.05 * u (rng)), ph = u (rng);
            const auto s = (std::size_t) i;
            osc0[s] = std::sin (2 * pi * ph), osc1[s] = std::sin (2 * pi * (ph - f)), oscC[s] = 2.0 * std::cos (2 * pi * f);
            T[s] = i % 5 == 0 ? 0.0 : 0.5 * u (rng);
            A[s] = i % 7 == 0 ? 0.0 : 0.5 * u (rng);
            if (i % 11 == 0)
                T[s] = A[s] = 0.0; // a silent partial inside the bank
        }
        kp.resize ((std::size_t) m + 1);
        for (int j = 0; j <= m; ++j)
            kp[(std::size_t) j] = std::pow (0.9993, (double) j);
        acc.assign (8 * (std::size_t) m, 0.0);
    }

    bool same (const BankState& o) const
    {
        auto eq = [] (const std::vector<double>& a, const std::vector<double>& b) {
            return a.size() == b.size() && std::memcmp (a.data(), b.data(), sizeof (double) * a.size()) == 0;
        };
        return eq (osc0, o.osc0) && eq (osc1, o.osc1) && eq (acc, o.acc);
    }
};
} // namespace harm190kr

TEST (perf190kr_harmless_bank_kernel_matches_the_one_group_loop)
{
    using namespace harm190kr;
    int cases = 0, same = 0, avxCases = 0;
    for (int top = 8; top <= 528; top += 8)
        for (const int m : { 1, 3, 8, 44, 87, 88, 1024 })
            for (int path = 0; path < 2; ++path)
            {
                BankState ref (m, (unsigned) (top * 1009 + m)), got (m, (unsigned) (top * 1009 + m));
                bool ran = true;
                for (int pass = 0; pass < 3 && ran; ++pass) // three segments: the states carry over
                {
                    bankKernelReference (ref.osc0.data(), ref.osc1.data(), ref.oscC.data(), ref.T.data(), ref.A.data(), top, m,
                                         ref.kp.data(), ref.acc.data());
                    ran = lynx::harmlessBankKernelForTest (path, got.osc0.data(), got.osc1.data(), got.oscC.data(), got.T.data(),
                                                           got.A.data(), top, m, got.kp.data(), got.acc.data());
                }
                if (! ran)
                    continue;
                ++cases;
                avxCases += path;
                if (ref.same (got))
                    ++same;
                else
                    std::printf ("  Harmless bank kernel differs: path %d, top %d, m %d\n", path, top, m);
            }
    std::printf ("  Harmless bank kernel vs the one-group loop: %d / %d bit-identical (%d of them AVX)\n", same, cases, avxCases);
    CHECK (cases > 0);
    CHECK (same == cases);
}

// Vocodex's band followers: the van Herk / Gil-Werman sliding maximum must give exactly what round 190kg's monotonic
// queue gave (verbatim below) — over windows from 1 to longer than the blocks, block lengths that cut the chunks
// anywhere, ties, zeros, and NaNs (the queue lets a NaN through on its own sample and forgets everything before it).
namespace harm190kr
{
struct SlidingMaxQueue
{
    int window = 1;
    std::vector<float> value;
    std::vector<std::uint32_t> when;
    int head = 0, count = 0;
    std::uint32_t clock = 0;

    explicit SlidingMaxQueue (int w)
    {
        window = std::max (1, w);
        value.assign ((std::size_t) window, 0.0f);
        when.assign ((std::size_t) window, 0);
    }

    void run (float* e, int n) noexcept
    {
        if (window <= 1)
            return;
        for (int k = 0; k < n; ++k, ++clock)
        {
            while (count > 0 && clock - when[(std::size_t) head] >= (std::uint32_t) window)
            {
                head = head + 1 == window ? 0 : head + 1;
                --count;
            }
            const float x = e[k];
            while (count > 0)
            {
                auto back = head + count - 1;
                if (back >= window)
                    back -= window;
                if (value[(std::size_t) back] > x)
                    break;
                --count;
            }
            auto pos = head + count;
            if (pos >= window)
                pos -= window;
            value[(std::size_t) pos] = x;
            when[(std::size_t) pos] = clock;
            ++count;
            e[k] = value[(std::size_t) head];
        }
    }
};
} // namespace harm190kr

TEST (perf190kr_vocodex_sliding_max_matches_the_queue)
{
    using namespace harm190kr;
    int cases = 0, same = 0;
    for (const int w : { 1, 2, 3, 7, 30, 41, 69, 81, 300 })
        for (const int block : { 1, 7, 30, 64, 480 })
            for (int kind = 0; kind < 3; ++kind)
            {
                std::mt19937 rng ((unsigned) (w * 31 + block * 7 + kind));
                std::uniform_real_distribution<float> u (0.0f, 1.0f);
                std::vector<float> x (3000);
                for (std::size_t i = 0; i < x.size(); ++i)
                {
                    // an envelope with plateaus (ties), exact zeros, decays and bursts
                    const auto t = (float) i;
                    x[i] = (i / 200) % 3 == 0 ? 0.0f : (i % 50 < 10 ? 0.5f : std::fabs (std::sin (t * 0.01f)) * u (rng));
                    if (kind == 1 && i % 997 == 500)
                        x[i] = std::numeric_limits<float>::quiet_NaN(); // a NaN now and then
                    if (kind == 2 && i >= 1200 && i < 1203)
                        x[i] = std::numeric_limits<float>::quiet_NaN(); // a short run of NaNs
                }
                auto ref = x;
                SlidingMaxQueue q (w);
                for (std::size_t at = 0; at < ref.size(); at += (std::size_t) block)
                    q.run (ref.data() + at, (int) std::min<std::size_t> ((std::size_t) block, ref.size() - at));
                const auto got = lynx::vocodex::slidingMaxForTest (w, x, block);
                ++cases;
                if (got.size() == ref.size() && std::memcmp (got.data(), ref.data(), sizeof (float) * ref.size()) == 0)
                    ++same;
                else
                    std::printf ("  Vocodex sliding max differs from the queue: window %d, block %d, input %d\n", w, block, kind);
            }
    std::printf ("  Vocodex sliding max vs round 190kg's queue: %d / %d bit-identical\n", same, cases);
    CHECK (same == cases);
}
