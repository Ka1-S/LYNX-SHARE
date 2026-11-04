#include "core/Drumaxx.h"
#include "core/DrumParamRanges.h"
#include "core/HostSlide.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>

// Port of the Drumaxx engine as decoded in REA_rodada67 §6–§9 (engine_x64.dll: DMSynth, DMPadGenerator). Float
// arithmetic follows the DLL's SSE code operation by operation (single precision where it computes in float, double
// where it converts), so FP contraction stays off.
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize ("fp-contract=off")
#endif

namespace lynx
{
namespace
{
constexpr int padCount = 16;
constexpr int padParams = 44;   // 0x2c per pad
constexpr int paramCount = 720; // 16 pads + master (0x2c1 level, 0x2c2 swing, 0x2c3 EQ on, 0x2c4…0x2cb bands, 0x2cc…0x2ce limiter)
constexpr int lineCount = 128;
constexpr float internalRate = 38000.0f;

std::uint32_t u32At (const std::uint8_t* d) noexcept
{
    std::uint32_t v;
    std::memcpy (&v, d, 4);
    return v;
}

float clamp01 (float v) noexcept
{
    if (v <= 0.0f)
        v = 0.0f;
    if (1.0f <= v)
        v = 1.0f;
    return v;
}

// a float constant by its bits (the DLL's literal)
inline float fbits (std::uint32_t u) noexcept
{
    float f;
    std::memcpy (&f, &u, 4);
    return f;
}
const float tick38k = fbits (0x37dcc0ba);    // 1/38000 (0x1800782a0)
const float inv2pow32 = fbits (0x2f800000);  // 2^-32
const float sofScale = fbits (0x3925908c);   // 0x1800782ac
const float invLn21 = fbits (0x3ea82bbe);    // 0x180078300
const float inv127 = fbits (0x3c010204);     // 0x1800782c4
const float silenceLevel = fbits (0x38d1b717); // 0.0001

// The CRT's vector sine (SVML-style, 0x18005a370; |x| < 10000): reduction by n = round(|x|/π) with π in four parts, an
// odd degree-9 polynomial, the sign from n's parity and x's (constants are the routine's, by their bits)
inline float vectorSin (float x) noexcept
{
    std::uint32_t xb;
    std::memcpy (&xb, &x, 4);
    const auto sign = xb & 0x80000000u;
    const auto a = std::fabs (x);
    const auto nf = a * fbits (0x3ea2f983) + fbits (0x4b400000);
    std::uint32_t nb;
    std::memcpy (&nb, &nf, 4);
    const auto n = nf - fbits (0x4b400000);
    auto r = a - n * fbits (0x40490000);
    r = r - n * fbits (0x3a7da000);
    r = r - n * fbits (0x34222000);
    r = r - n * fbits (0x2cb4611a);
    const auto r2 = r * r;
    std::uint32_t rb;
    std::memcpy (&rb, &r, 4);
    rb ^= nb << 31;
    std::memcpy (&r, &rb, 4);
    auto p = fbits (0x362edef8) * r2 + fbits (0xb94fb7ff);
    p = p * r2 + fbits (0x3c088766);
    p = p * r2 + fbits (0xbe2aaaa6);
    auto t = r2 * p;
    t = t * r;
    auto y = r + t;
    std::uint32_t yb;
    std::memcpy (&yb, &y, 4);
    yb ^= sign;
    std::memcpy (&y, &yb, 4);
    return y;
}

// The CRT's sinf/cosf (0x18005c0c0/0x18005b2e0, the FMA paths this machine runs): the argument in double, reduced by
// n = trunc(|x|·2/π + 0.5) with π/2 in two parts, a degree-9 sine / degree-10 cosine polynomial in double (fused
// multiply-adds), the quadrant's sign, then rounded to float. (|x| < 2^24 here; beyond, the CRT's Payne–Hanek path.)
inline double amdSinPoly (double r) noexcept
{
    const auto x2 = r * r;
    auto p = std::fma (x2, 2.7557319223985893e-06, -0.0001984126984126984);
    p = std::fma (p, x2, 0.008333333333333333);
    p = std::fma (p, x2, -0.16666666666666666);
    return std::fma (p, r * x2, r);
}

inline double amdCosPoly (double r) noexcept
{
    const auto x2 = r * r;
    const auto t = 1.0 - x2 * 0.5;
    auto p = std::fma (x2, -2.755731922398589e-07, 2.4801587301587298e-05);
    p = std::fma (p, x2, -0.0013888888888888887);
    p = std::fma (p, x2, 0.041666666666666664);
    return std::fma (p, x2 * x2, t);
}

inline void amdReduce (double ax, double& r, std::int64_t& region) noexcept
{
    const auto n = (std::int64_t) (std::int32_t) std::fma (ax, 0.6366197723675814, 0.5);
    region = n & 3;
    const auto dn = (double) (std::int32_t) n;
    const auto r1 = std::fma (-dn, 1.5707963267341256, ax);
    const auto r2 = dn * 6.077100506506192e-11;
    r = r1 - r2;
}

inline float crtSinf (float xf) noexcept
{
    const auto x = (double) xf;
    const auto ax = std::fabs (x);
    if (ax <= 0.7853981633974483)
    {
        if (ax < 0.0001220703125)
            return xf;
        if (ax < 0.0078125)
            return (float) std::fma (-(x * x * x), 0.16666666666666666, x);
        return (float) amdSinPoly (x);
    }
    double r;
    std::int64_t region;
    amdReduce (ax, r, region);
    auto y = (region & 1) == 0 ? amdSinPoly (r) : amdCosPoly (r);
    const auto flip = (region == 2 || region == 3) != std::signbit (x);
    return (float) (flip ? -y : y);
}

inline float crtCosf (float xf) noexcept
{
    const auto x = (double) xf;
    const auto ax = std::fabs (x);
    if (ax <= 0.7853981633974483)
    {
        if (ax < 0.0001220703125)
            return 1.0f;
        if (ax < 0.0078125)
            return (float) std::fma (-(x * 0.5), x, 1.0);
        return (float) amdCosPoly (x);
    }
    double r;
    std::int64_t region;
    amdReduce (ax, r, region);
    auto y = (region & 1) != 0 ? amdSinPoly (r) : amdCosPoly (r);
    return (float) ((((region + 1) >> 1) & 1) != 0 ? -y : y);
}

// The CRT's powf (0x180046690, the FMA path this machine runs), for x > 0 and finite y (else the C library's): ln x in
// double — near 1 (|x − 1| < 1/16) from s = u/(u + 2) with an odd series, else from the exponent, a 256-entry
// ln(1 + i/256) table at the rounded top mantissa bits and a cubic in the reduced remainder —, then e^(y·ln x) with
// k = round(t·64/ln 2), a 2^(j/64) table and a cubic (the near-1 branch uses the unfused exp the routine falls into),
// rounded to float. The tables are the exact mathematical values rounded to nearest (computed for LYNX, not copied).
// ln(1 + i/256), i = 0…256, each rounded to the nearest double
alignas (64) constexpr double crtLnTable[257] = {
    0x0.0p+0, 0x1.ff00aa2b10bc0p-9, 0x1.fe02a6b106789p-8, 0x1.7dc475f810a77p-7,
    0x1.fc0a8b0fc03e4p-7, 0x1.3cea44346a575p-6, 0x1.7b91b07d5b11bp-6, 0x1.b9fc027af9198p-6,
    0x1.f829b0e783300p-6, 0x1.1b0d98923d980p-5, 0x1.39e87b9febd60p-5, 0x1.58a5bafc8e4d5p-5,
    0x1.77458f632dcfcp-5, 0x1.95c830ec8e3ebp-5, 0x1.b42dd711971bfp-5, 0x1.d276b8adb0b52p-5,
    0x1.f0a30c01162a6p-5, 0x1.075983598e471p-4, 0x1.16536eea37ae1p-4, 0x1.253f62f0a1417p-4,
    0x1.341d7961bd1d1p-4, 0x1.42edcbea646f0p-4, 0x1.51b073f06183fp-4, 0x1.60658a93750c4p-4,
    0x1.6f0d28ae56b4cp-4, 0x1.7da766d7b12cdp-4, 0x1.8c345d6319b21p-4, 0x1.9ab42462033adp-4,
    0x1.a926d3a4ad563p-4, 0x1.b78c82bb0eda1p-4, 0x1.c5e548f5bc743p-4, 0x1.d4313d66cb35dp-4,
    0x1.e27076e2af2e6p-4, 0x1.f0a30c01162a6p-4, 0x1.fec9131dbeabbp-4, 0x1.0671512ca596ep-3,
    0x1.0d77e7cd08e59p-3, 0x1.14785846742acp-3, 0x1.1b72ad52f67a0p-3, 0x1.2266f190a5acbp-3,
    0x1.29552f81ff523p-3, 0x1.303d718e47fd3p-3, 0x1.371fc201e8f74p-3, 0x1.3dfc2b0ecc62ap-3,
    0x1.44d2b6ccb7d1ep-3, 0x1.4ba36f39a55e5p-3, 0x1.526e5e3a1b438p-3, 0x1.59338d9982086p-3,
    0x1.5ff3070a793d4p-3, 0x1.66acd4272ad51p-3, 0x1.6d60fe719d21dp-3, 0x1.740f8f54037a5p-3,
    0x1.7ab890210d909p-3, 0x1.815c0a14357ebp-3, 0x1.87fa06520c911p-3, 0x1.8e928de886d41p-3,
    0x1.9525a9cf456b4p-3, 0x1.9bb362e7dfb83p-3, 0x1.a23bc1fe2b563p-3, 0x1.a8becfc882f19p-3,
    0x1.af3c94e80bff3p-3, 0x1.b5b519e8fb5a4p-3, 0x1.bc286742d8cd6p-3, 0x1.c2968558c18c1p-3,
    0x1.c8ff7c79a9a22p-3, 0x1.cf6354e09c5dcp-3, 0x1.d5c216b4fbb91p-3, 0x1.dc1bca0abec7dp-3,
    0x1.e27076e2af2e6p-3, 0x1.e8c0252aa5a60p-3, 0x1.ef0adcbdc5936p-3, 0x1.f550a564b7b37p-3,
    0x1.fb9186d5e3e2bp-3, 0x1.00e6c45ad501dp-2, 0x1.0402594b4d041p-2, 0x1.071b85fcd590dp-2,
    0x1.0a324e27390e3p-2, 0x1.0d46b579ab74bp-2, 0x1.1058bf9ae4ad5p-2, 0x1.136870293a8b0p-2,
    0x1.1675cababa60ep-2, 0x1.1980d2dd4236fp-2, 0x1.1c898c16999fbp-2, 0x1.1f8ff9e48a2f3p-2,
    0x1.22941fbcf7966p-2, 0x1.2596010df763ap-2, 0x1.2895a13de86a3p-2, 0x1.2b9303ab89d25p-2,
    0x1.2e8e2bae11d31p-2, 0x1.31871c9544185p-2, 0x1.347dd9a987d55p-2, 0x1.3772662bfd85bp-2,
    0x1.3a64c556945eap-2, 0x1.3d54fa5c1f710p-2, 0x1.404308686a7e4p-2, 0x1.432ef2a04e814p-2,
    0x1.4618bc21c5ec2p-2, 0x1.49006804009d1p-2, 0x1.4be5f957778a1p-2, 0x1.4ec973260026ap-2,
    0x1.51aad872df82dp-2, 0x1.548a2c3add263p-2, 0x1.5767717455a6cp-2, 0x1.5a42ab0f4cfe2p-2,
    0x1.5d1bdbf5809cap-2, 0x1.5ff3070a793d4p-2, 0x1.62c82f2b9c795p-2, 0x1.659b57303e1f3p-2,
    0x1.686c81e9b14afp-2, 0x1.6b3bb2235943ep-2, 0x1.6e08eaa2ba1e4p-2, 0x1.70d42e2789236p-2,
    0x1.739d7f6bbd007p-2, 0x1.7664e1239dbcfp-2, 0x1.792a55fdd47a2p-2, 0x1.7bede0a37afc0p-2,
    0x1.7eaf83b82afc3p-2, 0x1.816f41da0d496p-2, 0x1.842d1da1e8b17p-2, 0x1.86e919a330ba0p-2,
    0x1.89a3386c1425bp-2, 0x1.8c5b7c858b48bp-2, 0x1.8f11e873662c7p-2, 0x1.91c67eb45a83ep-2,
    0x1.947941c2116fbp-2, 0x1.972a341135158p-2, 0x1.99d958117e08bp-2, 0x1.9c86b02dc0863p-2,
    0x1.9f323ecbf984cp-2, 0x1.a1dc064d5b995p-2, 0x1.a484090e5bb0ap-2, 0x1.a72a4966bd9eap-2,
    0x1.a9cec9a9a084ap-2, 0x1.ac718c258b0e4p-2, 0x1.af1293247786bp-2, 0x1.b1b1e0ebdfc5bp-2,
    0x1.b44f77bcc8f63p-2, 0x1.b6eb59d3cf35ep-2, 0x1.b9858969310fbp-2, 0x1.bc1e08b0dad0ap-2,
    0x1.beb4d9da71b7cp-2, 0x1.c149ff115f027p-2, 0x1.c3dd7a7cdad4dp-2, 0x1.c66f4e3ff6ff8p-2,
    0x1.c8ff7c79a9a22p-2, 0x1.cb8e0744d7acap-2, 0x1.ce1af0b85f3ebp-2, 0x1.d0a63ae721e64p-2,
    0x1.d32fe7e00ebd5p-2, 0x1.d5b7f9ae2c684p-2, 0x1.d83e7258a2f3ep-2, 0x1.dac353e2c5954p-2,
    0x1.dd46a04c1c4a1p-2, 0x1.dfc859906d5b5p-2, 0x1.e24881a7c6c26p-2, 0x1.e4c71a8687704p-2,
    0x1.e744261d68788p-2, 0x1.e9bfa659861f5p-2, 0x1.ec399d2468cc0p-2, 0x1.eeb20c640ddf4p-2,
    0x1.f128f5faf06edp-2, 0x1.f39e5bc811e5cp-2, 0x1.f6123fa7028acp-2, 0x1.f884a36fe9ec2p-2,
    0x1.faf588f78f31fp-2, 0x1.fd64f20f61572p-2, 0x1.ffd2e0857f498p-2, 0x1.011fab125ff8ap-1,
    0x1.02552a5a5d0ffp-1, 0x1.0389eefce633bp-1, 0x1.04bdf9da926d2p-1, 0x1.05f14bd26459cp-1,
    0x1.0723e5c1cdf40p-1, 0x1.0855c884b450ep-1, 0x1.0986f4f573521p-1, 0x1.0ab76bece14d2p-1,
    0x1.0be72e4252a83p-1, 0x1.0d163ccb9d6b8p-1, 0x1.0e44985d1cc8cp-1, 0x1.0f7241c9b497dp-1,
    0x1.109f39e2d4c97p-1, 0x1.11cb81787ccf8p-1, 0x1.12f719593efbcp-1, 0x1.1422025243d45p-1,
    0x1.154c3d2f4d5eap-1, 0x1.1675cababa60ep-1, 0x1.179eabbd899a1p-1, 0x1.18c6e0ff5cf06p-1,
    0x1.19ee6b467c96fp-1, 0x1.1b154b57da29fp-1, 0x1.1c3b81f713c25p-1, 0x1.1d610fe677003p-1,
    0x1.1e85f5e7040d0p-1, 0x1.1faa34b87094cp-1, 0x1.20cdcd192ab6ep-1, 0x1.21f0bfc65beecp-1,
    0x1.23130d7bebf43p-1, 0x1.2434b6f483934p-1, 0x1.2555bce98f7cbp-1, 0x1.26762013430e0p-1,
    0x1.2795e1289b11bp-1, 0x1.28b500df60783p-1, 0x1.29d37fec2b08bp-1, 0x1.2af15f02640adp-1,
    0x1.2c0e9ed448e8cp-1, 0x1.2d2b4012edc9ep-1, 0x1.2e47436e40268p-1, 0x1.2f62a99509546p-1,
    0x1.307d7334f10bep-1, 0x1.3197a0fa7fe6ap-1, 0x1.32b1339121d71p-1, 0x1.33ca2ba328995p-1,
    0x1.34e289d9ce1d3p-1, 0x1.35fa4edd36ea0p-1, 0x1.37117b54747b6p-1, 0x1.38280fe58797fp-1,
    0x1.393e0d3562a1ap-1, 0x1.3a5373e7ebdfap-1, 0x1.3b68449fffc23p-1, 0x1.3c7c7fff73206p-1,
    0x1.3d9026a7156fbp-1, 0x1.3ea33936b2f5cp-1, 0x1.3fb5b84d16f42p-1, 0x1.40c7a4880dce9p-1,
    0x1.41d8fe84672aep-1, 0x1.42e9c6ddf80bfp-1, 0x1.43f9fe2f9ce67p-1, 0x1.4509a5133bb0ap-1,
    0x1.4618bc21c5ec2p-1, 0x1.472743f33aaadp-1, 0x1.48353d1ea88dfp-1, 0x1.4942a83a2fc07p-1,
    0x1.4a4f85db03ebbp-1, 0x1.4b5bd6956e274p-1, 0x1.4c679afccee3ap-1, 0x1.4d72d3a39fd00p-1,
    0x1.4e7d811b75bb1p-1, 0x1.4f87a3f5026e9p-1, 0x1.50913cc01686bp-1, 0x1.519a4c0ba3446p-1,
    0x1.52a2d265bc5abp-1, 0x1.53aad05b99b7dp-1, 0x1.54b2467999498p-1, 0x1.55b9354b40bcdp-1,
    0x1.56bf9d5b3f399p-1, 0x1.57c57f336f191p-1, 0x1.58cadb5cd7989p-1, 0x1.59cfb25fae87ep-1,
    0x1.5ad404c359f2dp-1, 0x1.5bd7d30e71c73p-1, 0x1.5cdb1dc6c1765p-1, 0x1.5ddde57149923p-1,
    0x1.5ee02a9241675p-1, 0x1.5fe1edad18919p-1, 0x1.60e32f44788d9p-1, 0x1.61e3efda46467p-1,
    0x1.62e42fefa39efp-1
};
// 2^(j/64), j = 0…63, each rounded to the nearest double
alignas (64) constexpr double crtExp2Table[64] = {
    0x1.0000000000000p+0, 0x1.02c9a3e778061p+0, 0x1.059b0d3158574p+0, 0x1.0874518759bc8p+0,
    0x1.0b5586cf9890fp+0, 0x1.0e3ec32d3d1a2p+0, 0x1.11301d0125b51p+0, 0x1.1429aaea92de0p+0,
    0x1.172b83c7d517bp+0, 0x1.1a35beb6fcb75p+0, 0x1.1d4873168b9aap+0, 0x1.2063b88628cd6p+0,
    0x1.2387a6e756238p+0, 0x1.26b4565e27cddp+0, 0x1.29e9df51fdee1p+0, 0x1.2d285a6e4030bp+0,
    0x1.306fe0a31b715p+0, 0x1.33c08b26416ffp+0, 0x1.371a7373aa9cbp+0, 0x1.3a7db34e59ff7p+0,
    0x1.3dea64c123422p+0, 0x1.4160a21f72e2ap+0, 0x1.44e086061892dp+0, 0x1.486a2b5c13cd0p+0,
    0x1.4bfdad5362a27p+0, 0x1.4f9b2769d2ca7p+0, 0x1.5342b569d4f82p+0, 0x1.56f4736b527dap+0,
    0x1.5ab07dd485429p+0, 0x1.5e76f15ad2148p+0, 0x1.6247eb03a5585p+0, 0x1.6623882552225p+0,
    0x1.6a09e667f3bcdp+0, 0x1.6dfb23c651a2fp+0, 0x1.71f75e8ec5f74p+0, 0x1.75feb564267c9p+0,
    0x1.7a11473eb0187p+0, 0x1.7e2f336cf4e62p+0, 0x1.82589994cce13p+0, 0x1.868d99b4492edp+0,
    0x1.8ace5422aa0dbp+0, 0x1.8f1ae99157736p+0, 0x1.93737b0cdc5e5p+0, 0x1.97d829fde4e50p+0,
    0x1.9c49182a3f090p+0, 0x1.a0c667b5de565p+0, 0x1.a5503b23e255dp+0, 0x1.a9e6b5579fdbfp+0,
    0x1.ae89f995ad3adp+0, 0x1.b33a2b84f15fbp+0, 0x1.b7f76f2fb5e47p+0, 0x1.bcc1e904bc1d2p+0,
    0x1.c199bdd85529cp+0, 0x1.c67f12e57d14bp+0, 0x1.cb720dcef9069p+0, 0x1.d072d4a07897cp+0,
    0x1.d5818dcfba487p+0, 0x1.da9e603db3285p+0, 0x1.dfc97337b9b5fp+0, 0x1.e502ee78b3ff6p+0,
    0x1.ea4afa2a490dap+0, 0x1.efa1bee615a27p+0, 0x1.f50765b6e4540p+0, 0x1.fa7c1819e90d8p+0
};

inline std::uint64_t bitsOf (double v) noexcept { std::uint64_t u; std::memcpy (&u, &v, 8); return u; }
inline double fromBits (std::uint64_t u) noexcept { double v; std::memcpy (&v, &u, 8); return v; }
inline std::int32_t roundToInt (double v) noexcept { return (std::int32_t) std::nearbyint (v); }
inline float crtPowf (float x, float y) noexcept
{
    std::uint32_t ux, uy;
    std::memcpy (&ux, &x, 4), std::memcpy (&uy, &y, 4);
    const auto ay = uy & 0x7fffffffu, ax = ux & 0x7fffffffu;
    if (ay >= 0x7f800000u || ax >= 0x7f800000u || (std::int32_t) ux <= 0)
        return std::pow (x, y);
    if (ay == 0)
        return 1.0f;
    if (uy == 0x3f800000u)
        return x;
    const auto xd = (double) x, yd = (double) y;
    double lnx;
    bool fused = true;
    const auto u = xd - 1.0;
    if (ux < 0x3f880000u && std::fabs (u) < 0.0625)
    {
        const auto s = u / (u + 2.0);
        const auto su = s * u;
        const auto s2 = s + s;
        const auto w = s2 * s2;
        const auto a0 = w * 0x1.9999999bac6d4p-7 + 0x1.55555555554e6p-4;
        const auto a1 = w * 0x1.c8034c85dfff0p-12 + 0x1.2492307f1519fp-9;
        const auto s3 = s2 * w;
        const auto s7 = (w * w) * s3;
        lnx = u + ((s7 * a1 + s3 * a0) - su);
        fused = false;
    }
    else
    {
        const auto bx = bitsOf (xd);
        const auto m = bx & 0xfffffffffffffull;
        const auto idx = (int) ((m >> 44) + ((m >> 43) & 1));
        const auto c = fromBits ((std::uint64_t) (idx | 0x3fe00) << 44);
        const auto f = fromBits (m | 0x3fe0000000000000ull);
        const auto e = (double) ((int) (bx >> 52) - 0x3ff);
        const auto r = (c - f) * (1.0 / c);
        auto t = std::fma (0x1.5555555555555p-2, r, 0.5);
        t = std::fma (t, r, 1.0);
        lnx = ((e * 0x1.62e42fefa39efp-1) + crtLnTable[idx]) - r * t;
    }
    const auto t = yd * lnx;
    if (t > 88.72283935546875)
        return std::numeric_limits<float>::infinity();
    if (! (t > -103.2789306640625))
        return 0.0f;
    const auto k = roundToInt (t * 0x1.71547652b82fep+6);
    double res;
    const auto T = crtExp2Table[k & 63];
    if (fused)
    {
        const auto r = std::fma (-(double) k, 0x1.62e42fefa39efp-7, t);
        auto q = std::fma (0x1.5555555555555p-3, r, 0.5);
        q = std::fma (q, r, 1.0);
        res = std::fma (r * q, T, T);
    }
    else
    {
        const auto r = t - (double) k * 0x1.62e42fefa39efp-7;
        const auto q = (r * r) * (0x1.5555555555555p-3 * r + 0.5) + r;
        res = q * T + T;
    }
    res = fromBits (bitsOf (res) + ((std::uint64_t) (std::int64_t) (k >> 6) << 52));
    return (float) res;
}

// tiny-value flush of the biquad designs (|x| < 1e-9 → 0)
inline void flushTiny (float& v) noexcept
{
    if (v < 1e-9f && -1e-9f < v)
        v = 0.0f;
}

// ------------------------------------------------------------------------------------------------------- state

struct Kit
{
    std::array<float, paramCount> P {};
    std::array<std::uint8_t, padCount> groups {};
    std::string name;
};

// "SSLF" + u32 size + the engine stream: tag DMS3…DMS5 (or DMD3…DMD5), 256-byte kit name, 720 floats, 16 patterns of
// 0x402 bytes, 0x800 bytes, 16 cut groups, 16 varint output buses (FUN_1800303a0)
bool parseState (const std::vector<std::uint8_t>& s, Kit& kit)
{
    if (s.size() < 12 || std::memcmp (s.data(), "SSLF", 4) != 0)
        return false;
    const auto size = (std::size_t) u32At (s.data() + 4);
    if (size == 0 || 8 + size > s.size())
        return false;
    const auto* d = s.data() + 8;
    const auto tag = u32At (d);
    const auto v3to5 = (std::uint32_t) (tag + 0xbbb2accdu) < 3 || (std::uint32_t) (tag + 0xbbb2bbcdu) < 3;
    if (! v3to5)
        return false; // (DMS1/DMS2 use older loaders: no installation state)
    std::size_t o = 4;
    auto left = (long) size - 4;
    if (0x100 < left)
    {
        const auto* n = reinterpret_cast<const char*> (d + 4);
        kit.name.assign (n, strnlen (n, 256));
        o = 0x104;
        left -= 0x100;
    }
    int i = 0;
    for (; i < paramCount; ++i)
    {
        if (left < 3)
            return false;
        std::memcpy (&kit.P[(std::size_t) i], d + o, 4);
        o += 4, left -= 4;
    }
    // patterns (not modelled: kept in the raw state), the 0x800-byte block, cut groups
    for (int p = 0; p < padCount; ++p)
    {
        if (left < 2 || left - 2 < 0x40)
            return true;
        o += 0x402, left -= 0x402;
    }
    if (0x7ff < left)
        o += 0x800, left -= 0x800;
    for (int k = 0; k < padCount && 0 < left; ++k, ++o, --left)
        kit.groups[(std::size_t) k] = d[o];
    if (tag == 0x444d5333u || tag == 0x444d4433u) // version 3: EG retrig rescaled (×1/3)
        for (int k = 0; k < padCount; ++k)
        {
            auto& v = kit.P[(std::size_t) (k * padParams + 6)];
            auto f = v * 0.333333343f;
            if (f <= 0.0f)
                f = 0.0f;
            if (1.0f <= f)
                f = 1.0f;
            v = f;
        }
    return true;
}

// ------------------------------------------------------------------------------------------------------- filters

// FUN_18001bed0: shelf/peak designs (type 0 low shelf, 1 peak, 2 high shelf), DF1 states first
struct Biquad
{
    float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    float b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float rate = 44100.0f, gain = 0.0f, freq = 1000.0f, q = 1.0f;
    bool dirty = true;
    int type = 1;

    void design (float sr) noexcept
    {
        dirty = dirty || sr != rate;
        rate = sr;
        if (! dirty)
            return;
        dirty = false;
        const auto A = crtPowf (10.0f, gain * 0.05f);
        auto w = (float) (((double) freq * 6.283185) / (double) sr); // (0x180078410: 2π rounded to float)
        w = std::min (w, 3.0f);
        w = std::max (w, 0.0f);
        const auto s = crtSinf (w);
        const auto sh = std::sinh ((((double) q * 0.3465736) * (double) w) / (double) s);
        const auto al = (float) (sh + sh);
        const auto sqA = std::sqrt (A);
        auto x8 = (s * al) * 0.5f;
        const auto x13 = sqA * al;
        float norm = 0.0f;
        if (type == 0)
        {
            const auto c = crtCosf (w);
            const auto t = s * x13;
            const auto am1 = A - 1.0f, ap1 = A + 1.0f;
            const auto p = am1 * c;
            const auto e = ap1 - p;
            const auto f = p + ap1;
            const auto ap1c = ap1 * c;
            b0 = (e + t) * A;
            b2 = (e - t) * A;
            b1 = (A + A) * (am1 - ap1c);
            a1 = am1 * -2.0f - (ap1c + ap1c);
            a2 = f - t;
            norm = t + f;
        }
        else if (type == 1)
        {
            const auto x6 = x8 / A;
            x8 = x8 * A;
            b0 = x8 + 1.0f;
            norm = x6 + 1.0f;
            const auto c = crtCosf (w) * -2.0f;
            a1 = c, b1 = c;
            a2 = 1.0f - x6;
            b2 = 1.0f - x8;
        }
        else if (type == 2)
        {
            const auto c = crtCosf (w);
            const auto t = s * x13;
            const auto am1 = A - 1.0f, ap1 = A + 1.0f;
            const auto p = am1 * c;
            const auto f = p + ap1;
            const auto e = ap1 - p;
            norm = e + t;
            b0 = (t + f) * A;
            b2 = (f - t) * A;
            b1 = ((ap1 * c + am1) * A) * -2.0f;
            a1 = (am1 - ap1 * c) + (am1 - ap1 * c);
            a2 = e - t;
        }
        else
            return;
        const auto r = 1.0f / norm;
        b0 = r * b0, b1 = r * b1, b2 = r * b2, a1 = r * a1, a2 = r * a2;
        flushTiny (b0), flushTiny (b1), flushTiny (b2), flushTiny (a1), flushTiny (a2);
    }

    // FUN_18001c270
    float process (float x) noexcept
    {
        flushTiny (x);
        auto y = ((x1 * b1 + x * b0 + x2 * b2) - y1 * a1) - y2 * a2;
        flushTiny (y);
        x2 = x1, y2 = y1, x1 = x, y1 = y;
        return y;
    }

    void reset() noexcept { x1 = x2 = y1 = y2 = 0.0f; }
};

// ------------------------------------------------------------------------------------------------------- pad

struct Pad
{
    int index = 0;
    float fs = 44100.0f, vel = 0.0f, pitch48 = 1.0f;
    bool choked = false, active = false, attack = false;
    // mallet / membrane parameters (c970)
    float phase = 0, tension = 0, material = 0;
    int shape = 0x80, size = 0, first = 0, count = 0;
    float malletInit = 0, malletDecay = 0, mallet = 0, noiseAmt = 0, noiseDry = 0;
    bool noiseRetrig = false;
    std::uint32_t noiseSeedParam = 0, seed = 0;
    bool tableDirty = false;
    float fb = 0, cut = 0;
    // SOF
    float sofFreq = 0, sofDecay = 0, sofLevel = 0, s0 = 0, s1 = 0, s2 = 0, s3 = 0, sofCoef = 0, sofK = 0, sofG = 1.0f;
    bool sofOn = false;
    // LOW high-pass (FUN_18001bd70)
    float lowF = 0, lowQ = 0, lowRate = 0, lc0 = 0, lc1 = 0, lc2 = 0;
    float lowX1 = 0, lowX2 = 0, lowY1 = 0, lowY2 = 0;
    bool lowOn = false;
    float lowFreqParam = 0, lowResParam = 0;
    // HIGH shelf
    Biquad high;
    float highFreq = 0, highQ = 0, highGain = 0;
    bool highOn = false;
    // MID peak (enveloped)
    Biquad mid;
    float midFreq = 0, midFreqMod = 0, midLevel = 0, midLevelMod = 0, midQ = 0, midDecay = 0;
    bool midLevelOn = false, midModOn = false;
    float midEnv = 1.0f, midDecayNow = 0;
    int midRetrig = 0, midRetrigNow = 0, midCounter = 0;
    // output stage
    int lofi = 0;
    float hold = 0, env = 1.0f, holdTime = 0, silence = 0;
    double pitch39b8 = 1.0;
    // resampling
    double pos = 0.0;
    int countPrev = 0;
    float ratio = 1.0f, gL = -1.0f, gR = -1.0f, tL = 0.0f, tR = 0.0f;
    std::array<float, 1025> buf {}; // [0] = the previous block's last sample (+0x39d0)
    // delay lines
    std::array<std::vector<float>, lineCount> lines;
    std::array<int, lineCount> w {};
    std::array<float, 512> lp {}, weight {};
    std::uint32_t globalSeedTouch = 0;

    Pad()
    {
        for (int k = 0; k < lineCount; ++k)
            lines[(std::size_t) k].assign ((std::size_t) (4 * (512 - 4 * k)), 0.0f);
        mid.type = 1;
    }

    // FUN_18002d620 (Dispatcher 4 and the silence stop)
    void reset (float rate) noexcept
    {
        if (rate != 0.0f)
            fs = rate;
        if (0.0f < fs)
            ratio = internalRate / fs;
        active = false, attack = false;
        silence = 0.0f;
        midEnv = 1.0f;
        for (int k = 0; k < lineCount; ++k)
        {
            std::fill (lines[(std::size_t) k].begin(), lines[(std::size_t) k].end(), 0.0f);
            w[(std::size_t) k] = 0;
        }
        lp.fill (0.0f);
        std::fill (buf.begin(), buf.end(), 0.0f);
        mid.reset();
        env = 1.0f;
        lowX1 = lowX2 = lowY1 = lowY2 = 0.0f;
        high.reset();
        gL = -1.0f, gR = -1.0f;
        s0 = s1 = s2 = s3 = 0.0f;
        sofG = 1.0f;
    }
};

class Engine
{
public:
    Kit kit;
    std::array<Pad, padCount> pads;
    std::array<float, padCount> vol {}, pan {};
    std::array<float, 128> keyL {}, keyR {}, keyPitch {};
    std::array<Biquad, 8> eqL, eqR;
    float limC[2] = { 0, 0 }, limMin[2] = { 1, 1 };
    float level = 1.0f, outG[2] = { 0, 0 };
    float fs = 44100.0f;
    std::vector<float> mL, mR;

    Engine()
    {
        // FUN_18002c5b0: each pad generator steps a process-global LCG 1000 times and takes it as its noise seed
        std::uint32_t g = 0;
        for (int k = 0; k < padCount; ++k)
        {
            for (int i = 0; i < 1000; ++i)
                g = g * 0x1c3c718du + 0xf50d3da5u; // (−0xaf2c25b)
            pads[(std::size_t) k].seed = g;
            pads[(std::size_t) k].index = k;
        }
        keyL.fill (1.0f), keyR.fill (1.0f);
        mL.assign (512, 0.0f), mR.assign (512, 0.0f);
    }

    float p (int i) const noexcept { return kit.P[(std::size_t) i]; }

    void load (const Kit& k)
    {
        kit = k;
        derive();
    }

    // a live knob change (automation, round 168p): the pads read their parameters at every hit / block
    void setParam (int i, float v)
    {
        if (i < 0 || i >= (int) kit.P.size())
            return;
        kit.P[(std::size_t) i] = v;
        derive();
    }

    void derive()
    {
        for (int i = 0; i < padCount; ++i)
        {
            const auto v = p (i * padParams);
            vol[(std::size_t) i] = v * v + v * v;
            pan[(std::size_t) i] = p (i * padParams + 1);
        }
        level = crtPowf (10.0f, p (0x2c1) * 1.6f) * 0.25f;
    }

    // FUN_180033910: the master EQ's bands and the pads' rate
    void setRate (float rate)
    {
        fs = rate;
        for (auto& pd : pads)
            pd.reset (rate);
        const float freqs[8] = { 80, 160, 320, 640, 1280, 2560, 5120, 10240 };
        for (int b = 0; b < 8; ++b)
            for (auto* eq : { &eqL[(std::size_t) b], &eqR[(std::size_t) b] })
            {
                eq->freq = freqs[b], eq->q = 2.0f, eq->gain = 0.0f;
                eq->type = b == 0 ? 0 : b == 7 ? 2 : 1;
                eq->dirty = true;
                eq->reset();
            }
        limC[0] = limC[1] = 0.0f, limMin[0] = limMin[1] = 1.0f;
    }

    // FUN_180035700: a pad parameter through the four velocity modulations
    float vm (int pad, int j, float v) const noexcept
    {
        const auto base = pad * padParams;
        const auto v1 = p (base + j);
        auto v3 = v1;
        for (int m = 0; m < 4; ++m)
        {
            const auto t = (int) p (base + 35 + m);
            if (t != 0 && t + 6 == j)
                v3 = p (base + 39 + m);
        }
        return (1.0f - v) * v1 + v3 * v;
    }

    // FUN_18002c970
    void padParams_ (Pad& d) noexcept
    {
        const auto k = d.index;
        const auto v = d.vel;
        auto f = clamp01 (vm (k, 12, v));
        d.cut = f * f;
        f = clamp01 (vm (k, 11, v));
        d.fb = -crtPowf (f, 0.3f);
        f = clamp01 (vm (k, 14, v));
        if (f != d.phase)
            d.phase = f, d.tableDirty = true;
        f = clamp01 (vm (k, 13, v));
        if (f != d.tension)
            d.tension = f, d.tableDirty = true;
        f = clamp01 (vm (k, 15, v));
        if (f != d.material)
            d.material = f, d.tableDirty = true;
        f = clamp01 (1.0f - vm (k, 16, v));
        auto sz = (std::uint32_t) (std::int64_t) (f * 512.0f);
        if (0x1ff < sz)
            sz = 0x1ff;
        if ((int) sz != d.size)
            d.size = (int) sz, d.tableDirty = true;
        auto sh = (int) (vm (k, 17, v) * 128.0f);
        sh = sh < 1 ? 1 : sh > 0x80 ? 0x80 : sh;
        if (sh != d.shape)
            d.shape = sh, d.tableDirty = true;
        f = clamp01 (vm (k, 7, v));
        d.malletInit = (float) std::pow (10.0, (double) ((1.0f - f) * -4.0f));
        f = 1.0f - clamp01 (vm (k, 8, v));
        d.malletDecay = (float) ((double) (((f * 500.0f) * f) * (f * f) + 1.0f) * 0.0001);
        f = clamp01 (vm (k, 9, v));
        d.noiseAmt = f, d.noiseDry = 1.0f - f;
        d.noiseRetrig = p (5) != 0.0f; // (the plugin reads pad 1's "Noise Retrig" for every pad)
        d.noiseSeedParam = (std::uint32_t) (std::int64_t) ((double) vm (k, 10, v) * 4294967295.0);
        f = clamp01 (vm (k, 32, v));
        d.lofi = 0.01 <= (double) f ? (int) (std::pow ((double) (1.0f - f), 5.0) * 65536.0 + 4.0) : 0;
        f = clamp01 (vm (k, 33, v));
        d.holdTime = (f * f) * (f * 3.0f) + 0.001f;
        f = clamp01 (vm (k, 34, v));
        d.pitch39b8 = std::pow (2.0, (((double) f - 0.5) + (double) f) - 0.5);
        f = vm (k, 18, v);
        d.lowOn = f != 0.0f;
        d.lowFreqParam = crtPowf (2.0f, clamp01 (f) * 11.0f + 2.0f);
        d.lowResParam = clamp01 (vm (k, 19, v)) * 8.0f + 1.0f;
        d.highFreq = crtPowf (2.0f, clamp01 (vm (k, 29, v)) * 4.1f + 8.0f);
        f = clamp01 (vm (k, 31, v));
        d.highQ = (1.0f - f) + (1.0f - f) + 0.5f;
        f = clamp01 (vm (k, 30, v));
        d.highGain = (f - 0.5f) * 36.0f;
        d.highOn = 0.01f < std::fabs (d.highGain);
        d.midFreq = crtPowf (2.0f, clamp01 (vm (k, 23, v)) * 9.0f + 5.0f);
        d.midFreqMod = (clamp01 (vm (k, 26, v)) - 0.5f) * 32.0f;
        f = vm (k, 24, v);
        d.midLevelOn = f != 0.5f;
        d.midLevel = (clamp01 (f) - 0.5f) * 32.0f;
        f = vm (k, 27, v);
        d.midModOn = f != 0.5f;
        d.midLevelMod = (clamp01 (f) - 0.5f) * 48.0f;
        d.midQ = (1.0f - clamp01 (vm (k, 25, v))) * 4.0f + 0.5f;
        f = 1.0f - clamp01 (vm (k, 28, v));
        d.midDecay = (float) ((double) (((f * 1000.0f) * f) * (f * f) + 1.0f) * 1e-05);
        auto rt = (int) (p (6) * 3.0f); // (pad 1's "EG Retrig" for every pad)
        d.midRetrig = rt < 0 ? 0 : rt > 3 ? 3 : rt;
        d.sofFreq = clamp01 (vm (k, 20, v));
        f = clamp01 (vm (k, 21, v));
        d.sofDecay = (float) ((double) ((((f * 0.5f) * (f * 0.5f)) * 250.0f) * (f * f) + 1.0f) * 1e-05);
        f = vm (k, 22, v);
        d.sofOn = f != 0.0f;
        d.sofLevel = (float) (std::pow ((double) clamp01 (f), 8.0) * 0.5);
    }

    // FUN_18001bd70 (the LOW high-pass design)
    static void lowDesign (Pad& d, float freq, float res, float rate) noexcept
    {
        if (freq == d.lowF && res == d.lowQ && rate == d.lowRate)
            return;
        d.lowF = freq, d.lowQ = res, d.lowRate = rate;
        const auto wv = (float) (((double) freq * 6.283185307179586) / (double) rate);
        // the CRT's sincosf (0x1800599e0): the sine of [x, |x| + π/2 rounded to float] (below 2^-13: x, 1 − x²/2)
        float s, c;
        if (std::fabs (wv) >= 0.0001220703125f)
            s = vectorSin (wv), c = vectorSin (std::fabs (wv) + 1.57079637f);
        else
            s = wv, c = 1.0f - (wv * wv) * 0.5f;
        const auto r = 1.0f / (s + res);
        d.lc0 = (c + 1.0f) / (s / res + 2.0f);
        d.lc1 = (c * r) * (res * -2.0f);
        d.lc2 = (res - s) * r;
        if (d.lc0 < 1e-9f && -1e-9f < d.lc0)
            d.lc0 = 0.0f;
        if (d.lc1 < 1e-9f && -1e-9f < d.lc1)
            d.lc1 = 0.0f;
        if (d.lc2 < 1e-9f && -1e-9f < d.lc2)
            d.lc2 = 0.0f;
    }

    // FUN_18002d1a0 (after FUN_180035490 set the velocity)
    void trigger (Pad& d) noexcept
    {
        padParams_ (d);
        if (d.noiseRetrig)
            d.seed = d.noiseSeedParam;
        d.mallet = d.malletInit;
        d.midDecayNow = d.midDecay;
        d.midRetrigNow = d.midRetrig;
        d.choked = false;
        d.hold = 0.0f, d.silence = 0.0f;
        d.midEnv = 1.0f;
        if (d.tableDirty)
        {
            d.tableDirty = false;
            auto first = d.size / 4; // (size ≥ 0)
            first = std::clamp (first, 0, 0x7f);
            d.first = first;
            auto end = d.shape + d.size;
            int last;
            if (end >= 0x200)
                end = 0x1ff, last = 0x80;
            else
            {
                const auto q = end / 4;
                last = q * 4 < end ? q + 1 : q;
                last = std::min (last, 0x80);
            }
            d.count = last - first;
            const auto t = (d.material + d.material) - 1.0f;
            auto e = ((t * 20.0f) * t) * (t * t) + 1.0f;
            if (t < 0.0f)
                e = 1.0f / e;
            const auto step = ((d.tension * 0.5f) * d.tension) * (d.tension * d.tension);
            auto ph = d.phase;
            for (int j = 0x1ff; j >= 0; --j)
            {
                float val = 0.0f;
                if (! (j < d.size) && ! (end < j))
                {
                    auto x = ((ph - 0.5f) + ph) - 0.5f;
                    if (0.5f < x)
                        x = 1.0f - x;
                    if (x < -0.5f)
                        x = -1.0f - x;
                    x = x + x;
                    val = 0.0f <= x ? crtPowf (x, e) : -crtPowf (std::fabs (x), e);
                }
                ph = ph + step;
                d.weight[(std::size_t) j] = val;
                if (1.0f < ph)
                    ph = ph + -1.0f;
            }
        }
        lowDesign (d, d.lowFreqParam, d.lowResParam, internalRate);
        // the HIGH shelf (type 2)
        d.high.dirty = d.high.dirty || d.high.type != 2 || d.high.gain != d.highGain;
        d.high.type = 2;
        d.high.gain = d.highGain;
        const auto q = std::max (d.highQ, 0.0001f);
        d.high.dirty = d.high.dirty || q != d.high.q;
        d.high.q = q;
        const auto fr = std::max (d.highFreq, 0.0f);
        d.high.dirty = d.high.dirty || fr != d.high.freq;
        d.high.freq = fr;
        d.high.design (internalRate);
        d.midCounter = 0;
        auto c = crtPowf (2.0f, (float) ((double) (d.sofFreq * 0.5f + 0.1f) * 9.3 + 5.0)) * sofScale;
        c = c < 0.0f ? 0.0f : c > 1.0f ? 1.0f : c;
        d.sofCoef = c;
        d.sofG = 1.0f;
        d.active = true, d.attack = true;
        d.sofK = (float) ((double) c * 333.3333333333333);
    }

    // FUN_18002d8b0: n internal samples into out
    void generate (Pad& d, float* out, int n) noexcept
    {
        if (! d.active)
        {
            std::fill (out, out + n, 0.0f);
            return;
        }
        for (int i = 0; i < n; ++i)
        {
            const auto rnd = d.seed;
            d.seed = d.seed * 0xbb38435u + 0x3619636bu;
            const auto m = (float) ((1e-07 - (double) d.mallet) * (double) d.malletDecay + (double) d.mallet);
            d.mallet = m;
            const auto exc = (((float) rnd * inv2pow32 - 0.5f) * d.noiseAmt + d.noiseDry) * m;
            float acc[4] = { 0, 0, 0, 0 };
            for (int k = d.first; k < d.first + d.count; ++k)
            {
                auto& L = d.lines[(std::size_t) k];
                const auto len = 512 - 4 * k;
                auto& wp = d.w[(std::size_t) k];
                float* e = L.data();
                if (wp == len - 1)
                    e[4 * wp + 1] = e[1];
                if (wp == len - 2)
                    e[4 * wp + 2] = e[2], e[4 * wp + 4 + 2] = e[4 + 2];
                if (wp == len - 3)
                    e[4 * wp + 3] = e[3], e[4 * wp + 4 + 3] = e[4 + 3], e[4 * wp + 8 + 3] = e[8 + 3];
                float* r = e + 4 * wp;
                float* s = d.lp.data() + 4 * k;
                const float* wt = d.weight.data() + 4 * k;
                for (int l = 0; l < 4; ++l)
                {
                    const auto rv = r[l];
                    acc[l] = acc[l] + rv * wt[l];
                    const auto sv = (rv - s[l]) * d.cut + s[l];
                    s[l] = sv;
                    r[l] = sv * d.fb + exc;
                }
                if (++wp == len)
                {
                    for (int j = 0; j < len - 1; ++j)
                        e[4 * j + 1] = e[4 * j + 5];
                    for (int j = 0; j < len - 2; ++j)
                        e[4 * j + 2] = e[4 * j + 10];
                    for (int j = 0; j < len - 3; ++j)
                        e[4 * j + 3] = e[4 * j + 15];
                    wp = 0;
                }
            }
            auto y = ((acc[1] + acc[0]) + acc[2]) + acc[3];
            if (d.sofOn)
            {
                const auto x4 = d.s3;
                const auto den = (d.sofK + 1.0f) - d.sofG * d.sofK;
                auto x3 = y + x4 * -100000.0f;
                y = y + x4 * d.sofLevel;
                const auto coef = d.sofCoef / den;
                auto z = (float) ((double) x3 / ((double) std::fabs (x3) * 0.005 + 1.0));
                d.s0 = (z - d.s0) * coef + d.s0;
                d.s1 = (d.s0 - d.s1) * coef + d.s1;
                d.s2 = (d.s1 - d.s2) * coef + d.s2;
                d.sofG = d.sofG - d.sofG * d.sofDecay;
                d.s3 = (d.s2 - x4) * coef + x4;
            }
            if (d.lowOn)
            {
                const auto x = y;
                auto t = (x - (d.lowX1 + d.lowX1)) + d.lowX2;
                t = t * d.lc0 - d.lowY1 * d.lc1;
                t = t - d.lowY2 * d.lc2;
                d.lowX2 = d.lowX1, d.lowX1 = x;
                d.lowY2 = d.lowY1, d.lowY1 = t;
                y = t;
            }
            if (d.highOn)
            {
                auto& b = d.high;
                auto t = y * b.b0 + b.x1 * b.b1;
                t = t + b.x2 * b.b2;
                t = t - b.y1 * b.a1;
                t = t - b.y2 * b.a2;
                b.x2 = b.x1, b.x1 = y, b.y2 = b.y1, b.y1 = t;
                y = t;
            }
            if (d.midModOn || d.midLevelOn)
            {
                if (d.midCounter == 0)
                {
                    const auto t = 1.0f - d.midEnv;
                    const auto g = t * d.midLevelMod + d.midLevel;
                    auto dirty = g != d.mid.gain || d.mid.dirty;
                    d.mid.gain = g;
                    const auto x2 = d.midFreqMod * t;
                    float f;
                    if (d.midFreqMod >= 0.0f)
                        f = (x2 + 1.0f) * d.midFreq;
                    else
                        f = d.midFreq / (1.0f - x2);
                    f = std::max (f, 32.0f);
                    f = std::min (f, 12000.0f);
                    f = std::max (f, 0.0f);
                    dirty = dirty || f != d.mid.freq;
                    d.mid.freq = f;
                    const auto q = std::max (d.midQ, 0.0001f);
                    dirty = dirty || q != d.mid.q;
                    d.mid.q = q;
                    d.mid.dirty = dirty;
                    d.mid.design (internalRate);
                }
                if (++d.midCounter == 6)
                    d.midCounter = 0;
                d.midEnv = d.midEnv - d.midEnv * d.midDecayNow;
                if (d.midRetrigNow != 0 && (double) d.midEnv < 0.01)
                    d.midEnv = 1.0f, --d.midRetrigNow;
                auto& b = d.mid;
                auto t = y * b.b0 + b.x1 * b.b1;
                t = t + b.x2 * b.b2;
                t = t - b.y1 * b.a1;
                t = t - b.y2 * b.a2;
                b.x2 = b.x1, b.x1 = y, b.y2 = b.y1, b.y1 = t;
                y = t;
            }
            y = std::min (y, 1.0f);
            y = std::max (y, -1.0f);
            double e;
            if (d.choked || ! (d.hold < d.holdTime))
                e = (double) d.env - (double) d.env * 0.001;
            else
                e = (double) (1.0f - d.env) * 0.1 + (double) d.env;
            d.hold = d.hold + tick38k;
            d.env = (float) e;
            y = y * d.env;
            if (d.lofi != 0)
            {
                const auto lv = (float) d.lofi;
                y = (float) (int) (lv * y) / lv;
            }
            if (std::fabs (y) > silenceLevel)
                d.silence = 0.0f, d.attack = false;
            else
                d.silence = d.silence + tick38k;
            out[i] = y;
        }
        if ((! d.attack && 0.025 < (double) d.silence) || 0.5f < d.silence)
            d.reset (d.fs);
    }

    // FUN_18002e3b0: one pad, resampled to the host rate, added into l/r
    void renderPad (Pad& d, float* l, float* r, int n) noexcept
    {
        if (! d.active)
            return;
        const auto step = ((double) d.ratio * d.pitch39b8) * (double) d.pitch48;
        if (d.gL < 0.0f)
            d.gL = d.tL;
        if (d.gR < 0.0f)
            d.gR = d.tR;
        int done = 0;
        while (0 < n)
        {
            auto m = std::min (n, 256);
            n -= m;
            auto pos = d.pos;
            for (; pos < 0.0 && 0 < m; --m)
            {
                const auto x = (double) d.countPrev + pos;
                const auto i = (int) x;
                const auto b1 = d.buf[(std::size_t) i + 1], b0 = d.buf[(std::size_t) i];
                const auto v = (b1 - b0) * (float) (x - (double) i) + b0;
                l[done] = v * d.gL + l[done];
                r[done] = v * d.gR + r[done];
                ++done;
                pos = step + d.pos;
                d.pos = pos;
                d.gL = (d.tL - d.gL) * 0.001f + d.gL;
                d.gR = (d.tR - d.gR) * 0.001f + d.gR;
            }
            if (m != 0)
            {
                const auto x = (double) m * step + pos;
                auto cnt = (int) x;
                if ((double) cnt < x)
                    ++cnt;
                d.countPrev = cnt;
                generate (d, d.buf.data() + 1, cnt);
                for (int j = 0; j < m; ++j)
                {
                    const auto pp = d.pos;
                    const auto i = (int) pp;
                    const auto b0 = d.buf[(std::size_t) i], b1 = d.buf[(std::size_t) i + 1];
                    const auto v = (b1 - b0) * (float) (pp - (double) i) + b0;
                    l[done] = v * d.gL + l[done];
                    r[done] = v * d.gR + r[done];
                    ++done;
                    d.pos = step + d.pos;
                    d.gL = (d.tL - d.gL) * 0.001f + d.gL;
                    d.gR = (d.tR - d.gR) * 0.001f + d.gR;
                }
                d.pos = d.pos - (double) d.countPrev;
                d.buf[0] = d.buf[(std::size_t) d.countPrev];
            }
        }
        if ((! d.attack && 0.025 < (double) d.silence) || 0.5f < d.silence)
            d.reset (d.fs);
    }

    // FUN_180034e00 + FUN_180035490 (patterns not modelled)
    void noteOn (int key, float velocity) noexcept
    {
        if (velocity == 0.0f)
            return; // a note-off (it stops the running pattern: not modelled)
        bool solo = false;
        for (int k = 0; k < padCount; ++k)
            solo = solo || p (k * padParams + 3) != 0.0f;
        for (int k = 0; k < padCount; ++k)
        {
            auto play = p (k * padParams + 2) == 0.0f;
            if (solo && p (k * padParams + 3) == 0.0f)
                play = false;
            if ((int) p (k * padParams + 4) != key || ! play)
                continue;
            auto& d = pads[(std::size_t) k];
            d.vel = velocity;
            for (int j = 0; j < padCount; ++j)
                if (j != k && kit.groups[(std::size_t) j] != 0 && kit.groups[(std::size_t) j] == kit.groups[(std::size_t) k])
                    pads[(std::size_t) j].choked = true;
            trigger (d);
        }
    }

    // FUN_180034280 / FUN_180034990 / FUN_180035840 / FUN_180024870: one host block into l/r (overwrites)
    void render (float* l, float* r, int n)
    {
        std::fill (l, l + n, 0.0f);
        std::fill (r, r + n, 0.0f);
        for (int k = 0; k < padCount; ++k)
        {
            auto& d = pads[(std::size_t) k];
            if (! d.active)
                continue;
            auto key = (int) p (k * padParams + 4);
            key = std::clamp (key, 0, 127);
            auto pt = 0.0f * 12.0f + keyPitch[(std::size_t) key];
            pt = std::max (pt, -12.0f);
            pt = std::min (pt, 12.0f);
            d.pitch48 = crtPowf (2.0f, pt * 0.0833333358f);
            const auto pv = pan[(std::size_t) k], vv = vol[(std::size_t) k];
            d.tL = (1.0f - pv) * vv * keyL[(std::size_t) key];
            d.tR = pv * vv * keyR[(std::size_t) key];
        }
        for (int done = 0; done < n;)
        {
            const auto m = std::min (512, n - done);
            if ((int) mL.size() < m)
                mL.resize ((std::size_t) m), mR.resize ((std::size_t) m);
            std::fill (mL.begin(), mL.begin() + m, 0.0f);
            std::fill (mR.begin(), mR.begin() + m, 0.0f);
            bool any = false;
            for (auto& d : pads)
            {
                any = any || d.active;
                renderPad (d, mL.data(), mR.data(), m);
            }
            if (! any)
            {
                for (auto& b : eqL)
                    b.reset();
                for (auto& b : eqR)
                    b.reset();
                limC[0] = limC[1] = 0.0f, limMin[0] = limMin[1] = 1.0f;
            }
            else
                master (l + done, r + done, m);
            done += m;
        }
        // the plugin's output gain (FUN_180024870 with FUN_180025510): level·0.5, smoothed per sample
        const auto target = (level * 1.0f) * 0.5f;
        for (int i = 0; i < n; ++i)
        {
            l[i] = outG[0] * l[i];
            r[i] = r[i] * outG[1];
            outG[0] = (target - outG[0]) * 0.005f + outG[0];
            outG[1] = (target - outG[1]) * 0.005f + outG[1];
        }
    }

    // FUN_180035840
    void master (float* l, float* r, int n)
    {
        for (int b = 0; b < 8; ++b)
        {
            const auto g = (p (0x2c4 + b) - 0.5f) * 16.0f;
            for (auto* eq : { &eqL[(std::size_t) b], &eqR[(std::size_t) b] })
            {
                eq->dirty = eq->dirty || g != eq->gain;
                eq->gain = g;
            }
        }
        for (int b = 0; b < 8; ++b)
            eqL[(std::size_t) b].design (fs);
        for (int b = 0; b < 8; ++b)
            eqR[(std::size_t) b].design (fs);
        const auto eqOn = p (0x2c3) != 0.0f;
        const auto limOn = p (0x2cc) != 0.0f;
        const auto rel = p (0x2ce) * p (0x2ce) * 3.0f + 2.0f;
        const auto gain = crtPowf (10.0f, p (0x2cd) * 1.5f);
        for (int i = 0; i < n; ++i)
        {
            auto x = mL[(std::size_t) i], y = mR[(std::size_t) i];
            if (eqOn)
            {
                for (int b = 7; b >= 0; --b)
                    x = eqL[(std::size_t) b].process (x);
                for (int b = 7; b >= 0; --b)
                    y = eqR[(std::size_t) b].process (y);
            }
            if (limOn)
            {
                for (int c = 0; c < 2; ++c)
                {
                    auto& v = c == 0 ? x : y;
                    v = v * gain;
                    auto t = limC[c] * limC[c];
                    t = t * t * (1.0f - limMin[c]) + limMin[c];
                    if (1.0f < std::fabs (t * v))
                    {
                        limC[c] = 0.0f;
                        t = 1.0f / std::fabs (v);
                        limMin[c] = t;
                    }
                    v = t * v;
                    auto cc = (float) ((double) limC[c] + (double) rel / (double) fs);
                    limC[c] = 1.0f < cc ? 1.0f : cc;
                }
            }
            l[i] = x + l[i];
            r[i] = y + r[i];
        }
    }
};

// MIDI velocity of the C++ wrapper (FUN_180005fa0): trunc(127·ln(10·Init.Vol + 1)/ln 21), 0…127
int midiVelocity (double initVol) noexcept
{
    const auto v = (std::log ((float) initVol * 10.0f + 1.0f) * invLn21) * 127.0f;
    auto i = (int) v;
    return std::clamp (i, 0, 127);
}
} // namespace

bool drumaxxStateUsable (const std::vector<std::uint8_t>& state)
{
    Kit k;
    return parseState (state, k);
}

std::string drumaxxKitName (const std::vector<std::uint8_t>& state)
{
    Kit k;
    return parseState (state, k) ? k.name : std::string {};
}

std::string drumaxxPadName (const std::vector<std::uint8_t>& state, int pad)
{
    Kit k;
    if (pad < 0 || pad >= padCount || ! parseState (state, k))
        return {};
    // the 0x800-byte block after the patterns: 16 pad names of 128 bytes (round 168p)
    const std::size_t at = 8 + 0x104 + 4 * (std::size_t) paramCount + (std::size_t) padCount * 0x402 + 128 * (std::size_t) pad;
    if ((long) u32At (state.data() + 4) - 4 <= 0x100 || at + 128 > state.size() || at + 128 > 8 + (std::size_t) u32At (state.data() + 4))
        return {};
    const auto* n = reinterpret_cast<const char*> (state.data() + at);
    std::string name (n, strnlen (n, 128));
    for (const auto c : name)
        if ((unsigned char) c < 0x20)
            return {};
    return name;
}

namespace
{
// the byte offset of parameter `index` in a usable state with its kit name (−1 otherwise)
long paramOffset (const std::vector<std::uint8_t>& state, int index)
{
    Kit k;
    if (index < 0 || index >= paramCount || ! parseState (state, k) || (long) u32At (state.data() + 4) - 4 <= 0x100)
        return -1;
    return 8 + 4 + 0x100 + 4 * (long) index;
}
} // namespace

float drumaxxParam (const std::vector<std::uint8_t>& state, int index)
{
    const auto o = paramOffset (state, index);
    if (o < 0)
        return std::numeric_limits<float>::quiet_NaN();
    float v;
    std::memcpy (&v, state.data() + o, 4);
    return v;
}

void drumaxxSetParam (std::vector<std::uint8_t>& state, int index, float value)
{
    const auto o = paramOffset (state, index);
    if (o >= 0)
        std::memcpy (state.data() + o, &value, 4);
}

namespace
{
class DrumaxxInstrument final : public Instrument
{
public:
    DrumaxxInstrument() : engine (std::make_unique<Engine>()) {}

    void prepare (double sr) noexcept override
    {
        rate = sr;
        take();
        engine->setRate ((float) sr);
        settle();
    }

    void configure (const ChannelDef& channel, double) noexcept override
    {
        delete retired.exchange (nullptr);
        pitchCents.store (channel.sampler.pitch * 100.0f);
        if (channel.pluginState == state)
            return;
        auto k = std::make_unique<Kit>();
        if (! parseState (channel.pluginState, *k))
            return;
        state = channel.pluginState;
        delete incoming.exchange (k.release());
    }

    void setNextNoteExpression (float pan, float cents, bool) noexcept override { nextPan = pan, nextCents = cents; }

    // automation (round 168p): the clip's 0…1 over the knob's range, on the running kit
    void setParameter (int index, float value) noexcept override
    {
        take();
        const auto r = drumranges::drumaxx (index);
        if (r.valid && std::isfinite (value))
            engine->setParam (index, r.raw (value));
    }

    void noteOn (int key, float velocity) noexcept override
    {
        take();
        const auto k = std::clamp (key, 0, 127);
        const auto vel = std::clamp ((double) velocity, 0.0, 1.0);
        const auto initVol = (std::pow (11.0, 1.28 * vel) - 1.0) / 10.0;
        notePan[(std::size_t) k] = nextPan;
        notePlayed[(std::size_t) k] = true;
        keyLevels (k);
        const auto knob = pitchCents.load();
        keyCents[(std::size_t) k] = (double) knob + (double) nextCents;
        keyDown[(std::size_t) k] = true;
        keySlide[(std::size_t) k] = {};
        keySlide[(std::size_t) k].anchor (live.ref (knob));
        engine->keyPitch[(std::size_t) k] = (float) (keyCents[(std::size_t) k] / 100.0);
        engine->noteOn (k, (float) midiVelocity (initVol) * inv127);
        nextCents = 0.0f, nextPan = 0.0f;
    }

    // (one-shot pads; note-offs only stop patterns — and end the key's slides, round 167a)
    void noteOff (int key) noexcept override
    {
        if (key >= 0 && key < 128)
            keyDown[(std::size_t) key] = false;
    }

    // Drumaxx reads the host's per-note pitch every block (FUN_180034280, REA_rodada67 §7): a slide note or a live
    // PITCH knob / main pitch move bends the pads still ringing on that key (round 167a; a slide doesn't retrigger)
    bool followsChannelPorta() const noexcept override { return true; } // (round 168l: HostSlide::anchor)
    void slideTo (int key, float cents, float samples) noexcept override
    {
        const auto to = 100.0 * (std::clamp (key, 0, 127) - 60) + (double) pitchCents.load() + (double) cents;
        for (int k = 0; k < 128; ++k)
            if (keyDown[(std::size_t) k])
                keySlide[(std::size_t) k].start ((float) (to - (100.0 * (k - 60) + keyCents[(std::size_t) k])), samples);
    }
    void setChannelPitchNow (float knob, float rest) noexcept override
    {
        live.set (knob, rest);
        for (auto& sl : keySlide)
            sl.follow (live.now());
    }
    void allNotesOff() noexcept override {}

    bool idle() const noexcept override
    {
        for (const auto& d : engine->pads)
            if (d.active)
                return false;
        return true;
    }

    void render (float* left, float* right, int n, float gl, float gr) noexcept override
    {
        take();
        // FL's channel volume and pan reach the plugin as Final.Vol / Final.Pan (round 169l: through the host's
        // ComputeLRVol, see keyLevels); LYNX's channel gains are √2·V·cos/sin((1 + p)π/4), V and p recovered exactly
        const auto vol = std::sqrt (((double) gl * gl + (double) gr * gr) * 0.5);
        auto pan = vol > 0.0 ? std::atan2 ((double) gr, (double) gl) * (4.0 / 3.141592653589793) - 1.0 : 0.0;
        if (std::fabs (pan) < 1e-9)
            pan = 0.0;
        if (vol != channelVol || pan != channelPan)
        {
            channelVol = vol, channelPan = pan;
            for (int k = 0; k < 128; ++k) // (FL refreshes the per-key levels every block: a ringing pad follows the channel)
                if (notePlayed[(std::size_t) k])
                    keyLevels (k);
        }
        for (int k = 0; k < 128; ++k) // slides (round 167a): the key's Final.Pitch offset this block
            if (auto& sl = keySlide[(std::size_t) k]; sl.moving())
            {
                sl.advance ((float) n);
                engine->keyPitch[(std::size_t) k] = (float) ((keyCents[(std::size_t) k] + (double) sl.cents) / 100.0);
            }
        for (int done = 0; done < n;)
        {
            const auto m = std::min (blockSize, n - done);
            engine->render (bufL.data(), bufR.data(), m);
            for (int j = 0; j < m; ++j)
            {
                left[done + j] += bufL[(std::size_t) j];
                right[done + j] += bufR[(std::size_t) j];
            }
            done += m;
        }
    }

private:
    // The wrapper's per-key gains (engine_x64.dll FUN_180003e60, REA_rodada169l §1): every block it hands each held
    // voice's (Final.Pan, Final.Vol) to the host's ComputeLRVol (host slot 31, FL's law A: L = Vol·cos((1 + pan)π/4),
    // R = Vol·sin(…), 0.7071·Vol each at the centre) and stores L / R in the key tables the pads' gains scale by.
    // Final.Pan = the note's pan + the channel's, clamped.
    void keyLevels (int k) noexcept
    {
        const auto p = (float) std::clamp ((double) notePan[(std::size_t) k] + channelPan, -1.0, 1.0);
        const long double t = (1.0L + (long double) p) * (long double) 0.78539816339744828;
        engine->keyL[(std::size_t) k] = (float) (std::cos (t) * (long double) (float) channelVol);
        engine->keyR[(std::size_t) k] = (float) (std::sin (t) * (long double) (float) channelVol);
    }

    // FL renders the plugin from the moment it is loaded: its output gain has long reached the master level
    void settle() noexcept
    {
        const auto t = (engine->level * 1.0f) * 0.5f;
        engine->outG[0] = engine->outG[1] = t;
    }

    void take() noexcept
    {
        if (retired.load() != nullptr)
            return;
        if (auto* k = incoming.exchange (nullptr))
        {
            engine->load (*k);
            settle();
            retired.store (k);
        }
    }

    static constexpr int blockSize = 512;
    std::unique_ptr<Engine> engine;
    std::atomic<Kit*> incoming { nullptr }, retired { nullptr };
    std::atomic<float> pitchCents { 0.0f };
    std::array<double, 128> keyCents {};   // each key's Final.Pitch offset at its note on (cents)
    std::array<bool, 128> keyDown {};
    std::array<HostSlide, 128> keySlide {};
    LiveChannelPitch live; // (audio thread)
    std::vector<std::uint8_t> state;
    std::array<float, blockSize> bufL {}, bufR {};
    double rate = 44100.0, channelVol = 1.0, channelPan = 0.0;
    float nextCents = 0.0f, nextPan = 0.0f;
    std::array<float, 128> notePan {}; // each key's last note pan (FL's Init.Pan)
    std::array<bool, 128> notePlayed {};

public:
    ~DrumaxxInstrument() override
    {
        delete incoming.exchange (nullptr);
        delete retired.exchange (nullptr);
    }
};
} // namespace

std::unique_ptr<Instrument> makeDrumaxxInstrument() { return std::make_unique<DrumaxxInstrument>(); }

namespace drumaxx
{
std::vector<float> renderReference (const std::vector<std::uint8_t>& state, const std::vector<RefNote>& notes, double sampleRate,
                                    int length, int block)
{
    Kit kit;
    if (! parseState (state, kit) || length <= 0)
        return {};
    auto engine = std::make_unique<Engine>();
    engine->load (kit);
    engine->setRate ((float) sampleRate);
    block = std::max (1, block);
    std::vector<float> out (2 * (std::size_t) length, 0.0f), l ((std::size_t) block), r ((std::size_t) block);
    int n0 = 0;
    while (n0 < length)
    {
        auto ln = std::min (block, length - n0);
        for (const auto& nt : notes)
            if (n0 < nt.start && nt.start < n0 + ln)
                ln = nt.start - n0;
        for (const auto& nt : notes)
            if (nt.start == n0)
            {
                const auto k = std::clamp (nt.key, 0, 127);
                engine->keyL[(std::size_t) k] = engine->keyR[(std::size_t) k] = (float) nt.finalVol;
                {
                    // the host's Final.Pitch is a float (cents from C5): the offset is what is left of it after the key
                    const auto fp = 100.0f * (float) nt.key - 6000.0f + (float) nt.pitchOffset;
                    engine->keyPitch[(std::size_t) k] = (float) (((double) fp - 100.0 * (nt.key - 60)) / 100.0);
                }
                engine->noteOn (k, (float) midiVelocity (nt.initVol) * inv127);
            }
        engine->render (l.data(), r.data(), ln);
        for (int j = 0; j < ln; ++j)
        {
            out[2 * (std::size_t) (n0 + j)] = l[(std::size_t) j];
            out[2 * (std::size_t) (n0 + j) + 1] = r[(std::size_t) j];
        }
        n0 += ln;
    }
    return out;
}
} // namespace drumaxx
} // namespace lynx
