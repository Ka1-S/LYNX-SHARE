// Round beatfl190kr (0.1.30, perf-first): Harmless's sine-bank kernel as explicit SSE2 / AVX vectors with two groups
// of 8 partials per pass must give exactly what the one-group scalar loop of round 190kg's build gave. The reference
// below is that loop verbatim; both builds (SSE2, and AVX where the CPU has it) must match it bit for bit — the
// oscillator states and every lane sum — for every bank height, segment lengths around the control blocks, and
// partials whose amplitude and target are zero.

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

