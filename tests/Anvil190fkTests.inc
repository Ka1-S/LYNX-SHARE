// --- Round 190fk: Anvil Bus, LYNX's drum-bus engine (core/AnvilBus.h), and Live's Drum Buss imported onto it ---------
// The numbers are Live 12.4's own, measured by round 190ei on test signals (RESULTS.md §2); the engine holds them within
// the tolerances below (the round's A/B against the renders: /mnt/e/lynx-work/drumbus190fk/HANDOFF.md).

#include "core/AnvilBus.h"

namespace anvil190fk
{
using namespace lynx;

inline EffectParams params (std::initializer_list<std::pair<int, float>> values)
{
    auto p = makeEffectSlot (EffectKind::anvilBus).params;
    p[anvil::drive] = 0.0f; // Live's "all-zero" device unless set
    for (const auto& [k, v] : values)
        p[(std::size_t) k] = v;
    return p;
}

// the level (dBFS) of the `hz` component of the engine's output for a sine of `peakDb` dBFS at `hz` (lock-in over the
// last half of 0.6 s; the engine runs in 256-sample blocks)
inline double sineOut (const EffectParams& p, double peakDb, double hz = 1000.0, double probeHz = 0.0)
{
    anvil::Engine e;
    e.prepare (48000.0);
    const int n = 28800, block = 256;
    std::vector<float> l ((std::size_t) n), r ((std::size_t) n);
    const auto a = std::pow (10.0, peakDb / 20.0);
    for (int i = 0; i < n; ++i)
        l[(std::size_t) i] = r[(std::size_t) i] = (float) (a * std::sin (2.0 * 3.141592653589793 * hz * i / 48000.0));
    for (int at = 0; at < n; at += block)
        e.process (l.data() + at, r.data() + at, std::min (block, n - at), p.data());
    const auto f = probeHz > 0.0 ? probeHz : hz;
    double re = 0.0, im = 0.0, w = 0.0;
    for (int i = n / 2; i < n; ++i)
    {
        const auto win = 0.5 - 0.5 * std::cos (2.0 * 3.141592653589793 * (i - n / 2) / (n / 2));
        re += l[(std::size_t) i] * win * std::cos (2.0 * 3.141592653589793 * f * i / 48000.0);
        im += l[(std::size_t) i] * win * std::sin (2.0 * 3.141592653589793 * f * i / 48000.0);
        w += win;
    }
    return 20.0 * std::log10 (2.0 * std::sqrt (re * re + im * im) / w + 1.0e-15);
}
// the small-signal gain (dB) at `hz` relative to the all-zero device
inline double relGain (const EffectParams& p, double hz) { return sineOut (p, -40.0, hz) - sineOut (params ({}), -40.0, hz); }
} // namespace anvil190fk

TEST (anvil190fk_knob_laws_round_trip)
{
    using namespace lynx;
    namespace a = anvil;
    CHECK (std::abs (a::dampHz (a::dampValue (1000.0)) - 1000.0) < 0.01 && std::abs (a::dampHz (0.0f) - 500.0) < 1e-9 && std::abs (a::dampHz (1.0f) - 20000.0) < 1e-6);
    CHECK (std::abs (a::trimDb (a::trimValue (-12.0)) + 12.0) < 1e-4 && a::trimDb (1.0f) == 0.0 && std::abs (a::trimDb (0.0f) + 70.0) < 1e-9);
    CHECK (std::abs (a::outputDb (a::outputValue (0.0))) < 1e-5 && std::abs (a::outputDb (1.0f) - 12.0) < 1e-9 && std::abs (a::outputDb (0.0f) + 40.0) < 1e-9);
    CHECK (std::abs (a::transientsAmount (a::transientsValue (-0.5)) + 0.5) < 1e-6 && a::transientsAmount (0.5f) == 0.0);
    CHECK (std::abs (a::boomHz (a::boomHzValue (80.0)) - 80.0) < 1e-4);
    for (const auto t : { a::Type::soft, a::Type::medium, a::Type::hard })
        CHECK (a::typeOf (a::typeValue (t)) == t);
    // the measured drive gains [Live: +1.97 dB at 0; Soft 3.92 / 8.91 / 11.52, Medium 5.73 / 9.48 / 16.98, Hard 5.94 / 7.75 / 14.23]
    CHECK (std::abs (a::driveGainDb (a::Type::soft, 1.0) - 11.52) < 1e-9 && std::abs (a::driveGainDb (a::Type::medium, 0.25) - 5.73) < 1e-9);
    CHECK (std::abs (a::driveGainDb (a::Type::hard, 0.75) - (7.75 + 14.23) / 2.0) < 1e-9 && a::driveGainDb (a::Type::hard, 0.0) == 1.97);
}

TEST (anvil190fk_holds_lives_measured_levels)
{
    using namespace lynx;
    using anvil190fk::params;
    using anvil190fk::sineOut;
    using anvil190fk::relGain;
    namespace a = anvil;
    // the all-zero device is not transparent: +1.97 dB at 1 kHz, a 0 dBFS sine comes out at −0.93 dBFS
    const auto zero = params ({});
    CHECK (std::abs (sineOut (zero, -60.0) - (-58.03)) < 0.15);
    CHECK (std::abs (sineOut (zero, 0.0) - (-0.93)) < 0.6);
    // its colour: +4.4 dB at 100 Hz, +5.2 dB at 4 kHz (noise transfer function; small-signal sines here)
    CHECK (std::abs (sineOut (zero, -40.0, 100.0) - (-40.0 + 4.4)) < 0.7 && std::abs (sineOut (zero, -40.0, 4000.0) - (-40.0 + 5.2)) < 0.7);
    // the drive per type at 100 % (small signal, −60 dBFS)
    CHECK (std::abs (sineOut (params ({ { a::drive, 1.0f }, { a::driveType, a::typeValue (a::Type::soft) } }), -60.0) - (-60.0 + 11.52)) < 0.15);
    CHECK (std::abs (sineOut (params ({ { a::drive, 1.0f }, { a::driveType, a::typeValue (a::Type::medium) } }), -60.0) - (-60.0 + 16.98)) < 0.15);
    CHECK (std::abs (sineOut (params ({ { a::drive, 1.0f }, { a::driveType, a::typeValue (a::Type::hard) } }), -60.0) - (-60.0 + 14.23)) < 0.15);
    // Soft folds: at 100 % a 0 dBFS sine's fundamental all but vanishes [Live: −130 dBFS], 50 % at 0 dBFS: −2.0 dBFS
    CHECK (sineOut (params ({ { a::drive, 1.0f } }), 0.0) < -25.0);
    CHECK (std::abs (sineOut (params ({ { a::drive, 0.5f } }), 0.0) - (-1.99)) < 0.6);
    // Medium / Hard at 100 %: −12 dBFS in → −0.52 / −1.34 dBFS
    CHECK (std::abs (sineOut (params ({ { a::drive, 1.0f }, { a::driveType, 0.5f } }), -12.0) - (-0.52)) < 0.8);
    CHECK (std::abs (sineOut (params ({ { a::drive, 1.0f }, { a::driveType, 1.0f } }), -12.0) - (-1.34)) < 0.8);
    // Comp: +13.1 dB at low level, about 4 : 1 above −19 dBFS (−12 → −4.96, 0 → −2.04 dBFS)
    const auto comp = params ({ { a::comp, 1.0f } });
    CHECK (std::abs (sineOut (comp, -60.0) - (-46.90)) < 0.3);
    CHECK (std::abs (sineOut (comp, -12.0) - (-4.96)) < 0.5 && std::abs (sineOut (comp, 0.0) - (-2.04)) < 0.6);
    // Trim −12 dB: −10.03 dB at low level
    CHECK (std::abs (sineOut (params ({ { a::trim, a::trimValue (-12.0) } }), -60.0) - (-70.03)) < 0.15);
    // Damp: a first-order low pass, −3 dB at 1 kHz and −12.1 dB at 4 kHz for 1 kHz (relative to the all-zero device)
    const auto damp = params ({ { a::damp, a::dampValue (1000.0) } });
    CHECK (std::abs (relGain (damp, 1000.0) + 3.0) < 0.6 && std::abs (relGain (damp, 4000.0) + 12.1) < 1.0);
    // Boom 0.5 at 50 Hz, decay 1.0: +14.1 dB at 50 Hz; Crunch 0.5: +6.3 dB at 1 kHz
    const auto boom = params ({ { a::boom, 0.5f }, { a::boomFreq, a::boomHzValue (50.0) }, { a::boomDecay, 1.0f } });
    CHECK (std::abs (relGain (boom, 50.0) - 14.1) < 2.0);
    CHECK (std::abs (relGain (params ({ { a::crunch, 0.5f } }), 1000.0) - 6.3) < 0.5);
    // Dry/Wet 0.5: a linear blend with the raw input (Soft 100 % at −24 dBFS: half the dry plus half the driven)
    const auto dry = -24.0, wetDb = sineOut (params ({ { a::drive, 1.0f } }), -24.0);
    const auto mixDb = sineOut (params ({ { a::drive, 1.0f }, { a::dryWet, 0.5f } }), -24.0);
    CHECK (std::abs (std::pow (10.0, mixDb / 20.0) - 0.5 * (std::pow (10.0, dry / 20.0) + std::pow (10.0, wetDb / 20.0))) < 0.01);
}

TEST (anvil190fk_transients_follow_the_measured_direction)
{
    using namespace lynx;
    namespace a = anvil;
    // a hit every 0.5 s (a 1 kHz burst, 2 ms attack, 120 ms decay): + lifts the attack and the tail, − cuts the tail
    // [Live, drum loop: +1.0 → kick attack +13 dB, tail +10.6 dB; −0.5 → snare tail −3 dB]
    auto run = [] (float t, double from, double to) {
        anvil::Engine e;
        e.prepare (48000.0);
        auto p = anvil190fk::params ({ { a::transients, t } });
        const int n = 48000 * 2;
        std::vector<float> l ((std::size_t) n), r ((std::size_t) n);
        for (int i = 0; i < n; ++i)
        {
            const auto s = (double) (i % 24000) / 48000.0;
            const auto env = std::min (1.0, s / 0.002) * std::exp (-s / 0.12);
            l[(std::size_t) i] = r[(std::size_t) i] = (float) (0.3 * env * std::sin (2.0 * 3.141592653589793 * 1000.0 * i / 48000.0));
        }
        for (int at = 0; at < n; at += 256)
            e.process (l.data() + at, r.data() + at, std::min (256, n - at), p.data());
        double sum = 0.0;
        int count = 0;
        const auto lat = anvil::Engine::latency();
        for (int hit = 1; hit < 4; ++hit)
            for (int i = (int) ((hit * 0.5 + from) * 48000.0) + lat; i < (int) ((hit * 0.5 + to) * 48000.0) + lat; ++i, ++count)
                sum += (double) l[(std::size_t) i] * l[(std::size_t) i];
        return 10.0 * std::log10 (sum / std::max (1, count) + 1e-30);
    };
    const auto attack0 = run (0.5f, 0.0, 0.005), tail0 = run (0.5f, 0.2, 0.45);
    const auto attackUp = run (1.0f, 0.0, 0.005), tailUp = run (1.0f, 0.2, 0.45);
    const auto tailDown = run (0.25f, 0.2, 0.45);
    CHECK (attackUp - attack0 > 6.0 && tailUp - tail0 > 4.0);
    CHECK (tailDown - tail0 < -1.5);
}

TEST (anvil190fk_safe_at_the_extremes)
{
    using namespace lynx;
    namespace a = anvil;
    // every control at its ends, a +12 dBFS square wave then silence: finite, no denormals left, reset clean
    for (const float v : { 0.0f, 1.0f })
    {
        EffectParams p {};
        for (int k = 0; k < a::count; ++k)
            p[(std::size_t) k] = v;
        anvil::Engine e;
        e.prepare (44100.0);
        std::vector<float> l (4096), r (4096);
        bool finite = true;
        for (int b = 0; b < 40; ++b)
        {
            for (int i = 0; i < 4096; ++i)
                l[(std::size_t) i] = r[(std::size_t) i] = b < 20 ? ((i / 50) % 2 ? 4.0f : -4.0f) : 0.0f;
            e.process (l.data(), r.data(), 4096, p.data());
            for (int i = 0; i < 4096; ++i)
                finite = finite && std::isfinite (l[(std::size_t) i]) && std::isfinite (r[(std::size_t) i]);
        }
        CHECK (finite);
        CHECK (std::abs (l.back()) < 1.0e-6f);
        e.reset();
        std::fill (l.begin(), l.end(), 0.0f);
        std::fill (r.begin(), r.end(), 0.0f);
        e.process (l.data(), r.data(), 4096, p.data());
        CHECK (l.back() == 0.0f);
    }
}

TEST (anvil190fk_is_a_registered_built_in)
{
    using namespace lynx;
    const auto& info = effectInfo (EffectKind::anvilBus);
    CHECK (std::string (info.name) == "Anvil Bus" && info.params.size() == (std::size_t) anvil::count);
    auto fx = makeEffect (EffectKind::anvilBus);
    CHECK (fx != nullptr);
    if (fx)
    {
        fx->prepare (48000.0);
        CHECK (fx->latencySamples() == anvil::Engine::latency() && fx->latencySamples() > 0 && fx->latencySamples() < 188);
    }
    const auto slot = makeEffectSlot (EffectKind::anvilBus);
    CHECK (formatEffectParam (EffectKind::anvilBus, anvil::driveType, slot.params[anvil::driveType], 120.0) == "Soft");
    CHECK (formatEffectParam (EffectKind::anvilBus, anvil::boomFreq, slot.params[anvil::boomFreq], 120.0) == "50.0 Hz");
    CHECK (formatEffectParam (EffectKind::anvilBus, anvil::output, slot.params[anvil::output], 120.0) == "+0.0 dB");
    CHECK (formatEffectParam (EffectKind::anvilBus, anvil::transients, slot.params[anvil::transients], 120.0) == "+0%");
    CHECK (formatEffectParam (EffectKind::anvilBus, anvil::comp, 1.0f, 120.0) == "On");
    CHECK (presets::factory (EffectKind::anvilBus).size() >= 6);
    for (const auto& preset : presets::factory (EffectKind::anvilBus))
    {
        std::vector<std::string> unresolved;
        presets::apply (EffectKind::anvilBus, preset, 120.0, &unresolved);
        CHECK (unresolved.empty());
    }
    CHECK (plugui::layoutFor (EffectKind::anvilBus).items.size() > 10);
}

TEST (anvil190fk_live_drum_buss_imports_onto_anvil_bus)
{
    using namespace lynx;
    using namespace lynx::alsdev;
    namespace a = anvil;
    // Drive 50 % Medium, Damping 6 kHz, Transients −0.45, Boom 0.3 at 60 Hz decay 0.8, Comp on, Trim −6 dB, Output −3 dB,
    // Dry/Wet 41 %, Boom audition on
    const auto text = als190ek::drumBuss (0.5, 1, { { "CrunchAmount", 0.2 }, { "DampingFrequency", 6000.0 }, { "TransientShaping", -0.45 },
                                                    { "BoomFrequency", 60.0 }, { "BoomAmount", 0.3 }, { "BoomDecay", 0.8 },
                                                    { "InputTrim", 0.501187 }, { "OutputGain", 0.707946 }, { "DryWet", 0.41 } },
                                          als190ed::flag ("EnableCompression", true) + als190ed::flag ("BoomAudition", true));
    const auto t = als190ed::translate (text);
    CHECK (t.has_value() && t->slot.kind == EffectKind::anvilBus && t->to == "Anvil Bus");
    if (! t || t->slot.kind != EffectKind::anvilBus)
        return;
    const auto& p = t->slot.params;
    CHECK (p[a::drive] == 0.5f && a::typeOf (p[a::driveType]) == a::Type::medium && std::abs (p[a::crunch] - 0.2f) < 1e-6f);
    CHECK (std::abs (a::dampHz (p[a::damp]) - 6000.0) < 1.0 && std::abs (a::transientsAmount (p[a::transients]) + 0.45) < 1e-6);
    CHECK (std::abs (a::boomHz (p[a::boomFreq]) - 60.0) < 0.01 && std::abs (p[a::boom] - 0.3f) < 1e-6f && std::abs (p[a::boomDecay] - 0.8f) < 1e-6f);
    CHECK (p[a::comp] == 1.0f && std::abs (a::trimDb (p[a::trim]) + 6.0) < 0.01 && std::abs (a::outputDb (p[a::output]) + 3.0) < 0.01);
    CHECK (std::abs (p[a::dryWet] - 0.41f) < 1e-6f);
    // every Live control reaches its engine control (automation and rack macros), in Live's own units
    const auto* drive = als190cf::linkOf (*t, "DriveAmount");
    const auto* trans = als190cf::linkOf (*t, "TransientShaping");
    const auto* damp = als190cf::linkOf (*t, "DampingFrequency");
    const auto* trim = als190cf::linkOf (*t, "InputTrim");
    CHECK (drive != nullptr && drive->param == a::drive && drive->value (0.7) == 0.7f);
    CHECK (trans != nullptr && trans->param == a::transients && trans->value (-1.0) == 0.0f && trans->value (1.0) == 1.0f);
    CHECK (damp != nullptr && damp->value (20000.0) == 1.0f && std::abs (a::dampHz (damp->value (2000.0)) - 2000.0) < 0.5);
    CHECK (trim != nullptr && std::abs (a::trimDb (trim->value (0.251189)) + 12.0) < 0.01);
    CHECK (t->links.size() == 12);
    CHECK (als190bq::hasNote (t->notes.dropped, "Boom audition"));
    // the A/B path still builds round 190ek's rack
    const auto rackT = als190ed::translateRack (text);
    CHECK (rackT.has_value() && rackT->slot.kind == EffectKind::patcher && rack::isRack (rackT->slot));
}
