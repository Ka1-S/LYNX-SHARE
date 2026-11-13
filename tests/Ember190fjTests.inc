// Round 190fj: Ember Saturator's measured Tape (core/EmberTape.h: a Jiles–Atherton loop between a record and a playback
// EQ, the level law and the speed losses fitted black-box on the reference tape saturator's recorded renders,
// /mnt/e/lynx-work/ember190fj/HANDOFF.md) behind the Law switch: Measured for new instances, Classic — the tape curve of
// rounds 108 … 190, bit for bit — for projects saved before.

#include "Ember190fjRender.h"

namespace ember190fj
{
struct Case
{
    int idx[4];
    float val[4];
    double sr;
    int saved;    // ≥ 0: loaded as a project saved with this many parameters (upgradeLoadedEffectParams)
    bool classic; // Law set to Classic
    std::uint64_t hash;
    double energy, a, b;
};

inline Render renderCase (const Case& c)
{
    using namespace lynx;
    auto slot = makeEffectSlot (EffectKind::ember);
    if (c.saved >= 0)
        upgradeLoadedEffectParams (slot, (std::size_t) c.saved);
    if (c.classic)
        slot.params[(std::size_t) mixsuite::ember::law] = 0.0f;
    for (int k = 0; k < 4; ++k)
        if (c.idx[k] >= 0)
            slot.params[(std::size_t) c.idx[k]] = c.val[k];
    return render (slot, c.sr);
}

// the steady 1 kHz response of a single-band Tape at `driveDb` (Ember's 0 … 36 dB) to a sine of `levelDb` dBFS: gain in
// dB and the fundamental's phase in degrees (both against the input, the oversampler's latency removed)
inline std::pair<double, double> toneResponse (double driveDb, double levelDb, float speed = 0.25f, bool measured = true)
{
    using namespace lynx;
    namespace em = mixsuite::ember;
    auto slot = makeEffectSlot (EffectKind::ember);
    slot.params[(std::size_t) em::style] = 1.0f / 3.0f;
    slot.params[(std::size_t) em::drive] = (float) (driveDb / 36.0);
    slot.params[(std::size_t) em::tapeSpeed] = speed;
    slot.params[(std::size_t) em::law] = measured ? 1.0f : 0.0f;
    auto fx = makeEffect (slot);
    const double sr = 48000.0;
    fx->prepare (sr);
    const int n = 24000, lat = fx->latencySamples();
    std::vector<float> l ((std::size_t) n), r ((std::size_t) n);
    const double amp = std::pow (10.0, levelDb / 20.0);
    for (int i = 0; i < n; ++i)
        l[(std::size_t) i] = r[(std::size_t) i] = (float) (amp * std::sin (6.283185307179586 * 1000.0 * i / sr));
    for (int off = 0; off < n; off += 256)
        fx->process (l.data() + off, r.data() + off, std::min (256, n - off), slot.params, 120.0);
    double re = 0.0, im = 0.0;
    const int from = 12000, len = 9600; // whole periods
    for (int i = from; i < from + len; ++i)
    {
        const double ph = 6.283185307179586 * 1000.0 * (i - lat) / sr;
        re += l[(std::size_t) i] * std::sin (ph);
        im += l[(std::size_t) i] * std::cos (ph);
    }
    const double a = 2.0 * std::sqrt (re * re + im * im) / len;
    return { 20.0 * std::log10 (a / amp), std::atan2 (im, re) * 180.0 / 3.141592653589793 };
}
} // namespace ember190fj

TEST (ember190fj_law_and_tape_speed_are_appended_and_old_projects_keep_the_classic_tape)
{
    using namespace lynx;
    namespace em = mixsuite::ember;
    const auto& ps = effectInfo (EffectKind::ember).params;
    CHECK (ps.size() == (std::size_t) em::count && em::law == em::classicHot + 1 && em::tapeSpeed == em::law + 1);
    CHECK (std::string (ps[(std::size_t) em::law].name) == "Law" && ps[(std::size_t) em::law].defaultValue == 1.0f);
    CHECK (std::string (ps[(std::size_t) em::tapeSpeed].name) == "Tape speed" && ps[(std::size_t) em::tapeSpeed].defaultValue == 0.25f);
    CHECK ((std::size_t) em::count <= (std::size_t) maxEffectParams);
    auto fresh = makeEffectSlot (EffectKind::ember);
    upgradeLoadedEffectParams (fresh, (std::size_t) em::count);
    CHECK (fresh.params[(std::size_t) em::law] == 1.0f);
    auto old = makeEffectSlot (EffectKind::ember);
    upgradeLoadedEffectParams (old, (std::size_t) em::classicHot + 1);
    CHECK (old.params[(std::size_t) em::law] == 0.0f);
    CHECK (mixsuite::paramText (EffectKind::ember, em::law, 1.0f) == "Measured");
    CHECK (mixsuite::paramText (EffectKind::ember, em::law, 0.0f) == "Classic");
    CHECK (mixsuite::paramText (EffectKind::ember, em::tapeSpeed, 0.25f) == "15 ips");
    CHECK (mixsuite::paramText (EffectKind::ember, em::tapeSpeed, 1.0f) == "1.875 ips");
    CHECK (em::tapeSpeedIndex (0.0f) == 0 && em::tapeSpeedIndex (0.5f) == 2 && em::tapeSpeedIndex (1.0f) == 4);
    // the measured core runs only where the Classic tape curve ran: single band, Style Tape, no voice / character /
    // Classic voicing / feedback
    auto p = makeEffectSlot (EffectKind::ember).params;
    p[(std::size_t) em::style] = 1.0f / 3.0f;
    CHECK (em::measuredTape (p));
    p[(std::size_t) em::law] = 0.0f;
    CHECK (! em::measuredTape (p));
    p[(std::size_t) em::law] = 1.0f;
    p[(std::size_t) em::voice] = 0.2f;
    CHECK (! em::measuredTape (p));
    p[(std::size_t) em::voice] = 0.0f;
    p[(std::size_t) em::bands] = 0.34f;
    CHECK (! em::measuredTape (p));
    p[(std::size_t) em::bands] = 0.0f;
    p[(std::size_t) em::style] = 0.0f;
    CHECK (! em::measuredTape (p));
    // the panel: the speed while Tape runs measured, the law switch while a project's Classic tape is set
    const auto& l = plugui::layoutFor (EffectKind::ember);
    bool speed = false, law = false;
    for (const auto& item : l.items)
    {
        CHECK (item.x >= 0.0f && item.y >= 0.0f && item.x + item.w <= (float) l.width + 0.5f && item.y + item.h <= (float) l.height + 0.5f);
        if (item.param != nullptr && std::string (item.param) == "Tape speed")
            speed = item.whenParam != nullptr && item.when2Param != nullptr && item.when3Param != nullptr;
        if (item.param != nullptr && std::string (item.param) == "Law")
            law = item.whenParam != nullptr && item.when2Param != nullptr && item.when3Param != nullptr;
    }
    CHECK (speed && law);
}

TEST (ember190fj_classic_tape_and_the_other_paths_render_as_the_base_commit_did)
{
    using namespace lynx;
    // FNV-1a of the float bits, the energy and two samples, from the base commit b0cb90aec's objects
    // (/mnt/e/lynx-work/ember190fj/golden, golden.cpp -DBASE): Tape with the Classic law, an old project (upgraded to
    // Classic), and with the Measured law every path the measured core does not take (bands, other styles, the Classic
    // voicing, a voice)
    namespace em = mixsuite::ember;
    const ember190fj::Case cases[] = {
        { { -1, -1, -1, -1 }, {}, 48000.0, -1, false, 0x53a19ae70ce5406cull, 4990.1625, -0.331263304, 0.00500024157 }, // default
        { { em::style, em::drive, -1, -1 }, { 1.0f / 3.0f, 0.5f }, 48000.0, -1, true, 0xf0e86578739a2c24ull, 2193.41363, -0.221811995, 0.034259297 }, // tape d0.5 classic
        { { em::style, em::drive, em::tone, em::lowCut }, { 1.0f / 3.0f, 0.8f, 0.7f, 0.2f }, 44100.0, -1, true, 0xa5e0442d122dd1eaull, 2268.03394, -0.17549254, -0.169247657 }, // tape d0.8 tone lowcut 44k classic
        { { em::style, em::drive, -1, -1 }, { 1.0f / 3.0f, 0.6f }, 48000.0, em::classicHot + 1, false, 0x5801b2634b49a540ull, 2111.39431, -0.20686233, 0.0464245565 }, // tape d0.6 old project
        { { em::style, em::drive, em::bands, -1 }, { 1.0f / 3.0f, 0.5f, 0.34f }, 48000.0, -1, false, 0xbc8195172a35b9b4ull, 5312.00704, -0.0836353153, -0.033575356 }, // tape 2 bands
        { { em::style, em::drive, -1, -1 }, { 2.0f / 3.0f, 0.6f }, 48000.0, -1, false, 0xad1eaddbf40aca7eull, 2822.06092, -0.194525942, -0.00734378165 }, // transistor d0.6
        { { em::voicing, em::classicDrive, -1, -1 }, { 1.0f, 0.6f }, 48000.0, -1, false, 0x793413613690f0b2ull, 47007.7931, -0.850343585, -0.181251779 }, // classic voicing
        { { em::style, em::drive, em::voice, -1 }, { 1.0f / 3.0f, 0.5f, 0.2f }, 48000.0, -1, false, 0x48cc5eddb7f88ce9ull, 2774.99376, -0.237170994, 0.0424630567 }, // tape voice 1
    };
    int sameBits = 0, n = 0;
    for (const auto& c : cases)
    {
        const auto r = ember190fj::renderCase (c);
        CHECK (std::abs (r.energy - c.energy) <= 1e-4 * c.energy);
        CHECK (std::abs (r.a - c.a) < 2e-6 && std::abs (r.b - c.b) < 2e-6);
        sameBits += r.hash == c.hash ? 1 : 0;
        ++n;
    }
    // bit for bit where the maths library is the one the goldens were made with (glibc 2.43 on x86-64: the local builds);
    // other libm versions may round some transcendental calls differently (see Reverb190bzTests.inc)
    std::printf ("  ember classic paths: %d of %d renders hash-identical to the base commit\n", sameBits, n);
   #if defined (__linux__) && defined (__x86_64__) && defined (__GLIBC__) && (__GLIBC__ > 2 || __GLIBC_MINOR__ >= 43) \
       && ! defined (LYNX_VERB_HASH_OFF)
    CHECK (sameBits == n);
   #else
    (void) sameBits;
   #endif
}

TEST (ember190fj_measured_tape_renders_as_fitted)
{
    using namespace lynx;
    // regression goldens of the measured core (the branch's own renders, same hash / tolerance rules as above)
    namespace em = mixsuite::ember;
    const ember190fj::Case cases[] = {
        { { em::style, em::drive, -1, -1 }, { 1.0f / 3.0f, 0.5f }, 48000.0, -1, false, 0xb67612fe3014c6e2ull, 4676.95158, -0.326048017, -0.00826984271 }, // m tape d18 15ips
        { { em::style, em::drive, -1, -1 }, { 1.0f / 3.0f, 28.0f / 36.0f }, 44100.0, -1, false, 0xaef36a4aabcde62aull, 1898.97271, -0.24036774, -0.18424654 }, // m tape d28 15ips 44k
        { { em::style, em::drive, em::tapeSpeed, -1 }, { 1.0f / 3.0f, 33.0f / 36.0f, 1.0f }, 48000.0, -1, false, 0x0f4cc49d1799c8e3ull, 615.87348, -0.123516142, -0.0797277093 }, // m tape d33 1.875ips
        { { em::style, em::drive, em::tapeSpeed, em::autoGain }, { 1.0f / 3.0f, 22.0f / 36.0f, 0.0f, 0.0f }, 48000.0, -1, false, 0xb1317ed82f8bf30cull, 622164.199, -3.93330026, -0.223841012 }, // m tape d22 30ips no auto
    };
    int sameBits = 0, n = 0;
    for (const auto& c : cases)
    {
        const auto r = ember190fj::renderCase (c);
        CHECK (std::abs (r.energy - c.energy) <= 1e-4 * c.energy);
        CHECK (std::abs (r.a - c.a) < 2e-6 && std::abs (r.b - c.b) < 2e-6);
        sameBits += r.hash == c.hash ? 1 : 0;
        ++n;
    }
    std::printf ("  ember measured tape: %d of %d renders hash-identical to the goldens\n", sameBits, n);
   #if defined (__linux__) && defined (__x86_64__) && defined (__GLIBC__) && (__GLIBC__ > 2 || __GLIBC_MINOR__ >= 43) \
       && ! defined (LYNX_VERB_HASH_OFF)
    CHECK (sameBits == n);
   #else
    (void) sameBits;
   #endif
}

TEST (ember190fj_measured_tape_follows_the_reference_level_law)
{
    using namespace lynx;
    // [black-box, the reference's 1 kHz staircase (tp172c)] quiet levels pass at 0 dB at every drive (its compensated
    // drive); the 0 dBFS step comes out 1.2 dB down at its default (Ember 18 dB) and 8.8 dB down at its Drive +10 dB
    // (Ember 28 dB); at Drive +15 dB and 1⅞ ips the output holds a ceiling (−10.6 / −16.6 dB at −6 / 0 dBFS)
    for (const double d : { 18.0, 28.0 })
    {
        const auto quiet = ember190fj::toneResponse (d, -48.0);
        CHECK (std::abs (quiet.first) < 0.15);
        CHECK (std::abs (quiet.second) < 0.6);
    }
    const auto def0 = ember190fj::toneResponse (18.0, 0.0);
    const auto hot = ember190fj::toneResponse (28.0, 0.0);
    const auto slow = ember190fj::toneResponse (33.0, -6.0, 1.0f);
    const auto slowLouder = ember190fj::toneResponse (33.0, 0.0, 1.0f);
    std::printf ("  measured tape 1 kHz: drive 18 dB at 0 dBFS %+.2f dB, drive 28 dB %+.2f dB; 1.875 ips drive 33 dB at -6 / 0 dBFS %+.2f / %+.2f dB\n",
                 def0.first, hot.first, slow.first, slowLouder.first);
    CHECK (def0.first < -0.6 && def0.first > -2.0);
    CHECK (hot.first < -7.5 && hot.first > -10.5);
    CHECK (slow.first < -9.0 && slow.first > -13.0);
    CHECK (slowLouder.first < -15.0 && slowLouder.first > -19.0);
    // the Classic tape curve is static: it keeps the fundamental's phase
    const auto classicHot = ember190fj::toneResponse (28.0, 0.0, 0.25f, false);
    CHECK (std::abs (classicHot.second) < 0.6);

    // safety: silence after a loud burst decays to nothing, extreme drive + PUNISH + hot input stay finite, latency as
    // the Classic law's (the oversampler's)
    namespace em = mixsuite::ember;
    auto slot = makeEffectSlot (EffectKind::ember);
    slot.params[(std::size_t) em::style] = 1.0f / 3.0f;
    slot.params[(std::size_t) em::drive] = 1.0f;
    slot.params[(std::size_t) em::punish] = 1.0f;
    slot.params[(std::size_t) em::tapeSpeed] = 1.0f;
    auto fx = makeEffect (slot);
    fx->prepare (44100.0);
    std::vector<float> l (44100), r (44100);
    for (int i = 0; i < 8820; ++i)
        l[(std::size_t) i] = r[(std::size_t) i] = (float) (4.0 * std::sin (0.07 * i) + (i % 97 == 0 ? 3.0 : 0.0));
    bool finite = true;
    for (int off = 0; off < 44100; off += 512)
        fx->process (l.data() + off, r.data() + off, std::min (512, 44100 - off), slot.params, 120.0);
    float tail = 0.0f;
    for (int i = 0; i < 44100; ++i)
    {
        finite = finite && std::isfinite (l[(std::size_t) i]) && std::isfinite (r[(std::size_t) i]);
        if (i > 30000) tail = std::max (tail, std::abs (l[(std::size_t) i]));
    }
    CHECK (finite);
    CHECK (tail < 1e-5f);
    auto classicSlot = slot;
    classicSlot.params[(std::size_t) em::law] = 0.0f;
    auto fxClassic = makeEffect (classicSlot);
    fxClassic->prepare (44100.0);
    CHECK (fx->latencySamples() == fxClassic->latencySamples());
    CHECK (fx->latencySamples() == mixsuite::Oversampler::latency());
}
