// --- Fruity Scratcher DVS (core/Dvs.h, core/Scratcher.h) - round 167c (REA_rodada167c.md §1)

#include <complex>

#include "DvsSignal.h"
#include "core/Dvs.h"
#include "core/Scratcher.h"

TEST (dvs167_types_and_state)
{
    using namespace lynx;
    // FL's list: Off → −1, Timecode CD (last) → 0, the others keep their index [DECOMP 0x733fe0]
    CHECK (dvs::stateOfCombo (0) == -1 && dvs::stateOfCombo (1) == 1 && dvs::stateOfCombo (7) == 7 && dvs::stateOfCombo (8) == 0);
    for (int c = 0; c < dvs::comboCount; ++c)
        CHECK (dvs::comboOfState (dvs::stateOfCombo (c)) == c);
    CHECK (std::string (dvs::comboName (3)) == "Serato CD" && std::string (dvs::comboName (8)) == "Timecode CD");
    CHECK (dvs::supported (1) && dvs::supported (5) && ! dvs::supported (0) && ! dvs::supported (6) && ! dvs::supported (-1));
    // the formats [DECOMP engine_x64 0x1800037c0]
    CHECK (dvs::format (1)->length == 710000u && dvs::format (1)->offset == 25.7 && dvs::format (4)->resolution == 2000);
    CHECK (dvs::format (5)->bits == 23 && dvs::format (5)->flags == 0x70 && dvs::format (3)->taps == 0x34d54u);

    // the Timecode type parameter ↔ the state's index
    for (int st = -1; st <= 7; ++st)
        CHECK (scratcher::timecodeOf (scratcher::timecodeValue (st)) == st);
    CHECK (scratcher::paramText (scratcher::timecodeType, scratcher::timecodeValue (4)) == "Traktor Skratch Mk1, Side A");
    CHECK (scratcher::paramText (scratcher::riaaPreamp, 1.0f) == "On");

    // FL's state keeps the type and the RIAA byte (version 0xF462A)
    const auto d = scratcher::defaults();
    const auto blob = scratcher::toFl (d, "x.wav", 2, true);
    const auto st = scratcher::parse (blob);
    CHECK (st && st->timecode == 2 && st->riaa && st->samplePath == "x.wav");
    const auto off = scratcher::parse (scratcher::toFl (d, "", -1, false));
    CHECK (off && off->timecode == -1 && ! off->riaa);
    // the import's slot carries them
    const auto fx = effectFromFl ("Fruity Scratcher", blob, 140.0);
    CHECK (fx && scratcher::timecodeOf (fx->params[scratcher::timecodeType]) == 2 && fx->params[scratcher::riaaPreamp] == 1.0f);
}

TEST (dvs167_register_and_table)
{
    using namespace lynx;
    // the register's sequence: maximal length over the record (no repeat within it), Serato A
    const auto& f = *dvs::format (1);
    auto s = f.seed;
    std::set<std::uint32_t> seen;
    for (int i = 0; i < 20000; ++i)
    {
        seen.insert (s);
        s = dvs::nextState (s, f);
    }
    CHECK (seen.size() == 20000u);
    // the position table: a record decoded from its own start names the right place
    dvs::warm (1, true);
    dvs::warm (4, true);
    dvs::Decoder dec;
    dec.configure (1, 44100.0);
    const auto sig = dvstest::make (f, 0.0, [] (double) { return 1.0; }, 44100, 44100.0);
    for (int i = 0; i < 44100; ++i)
        dec.process (sig.left[(std::size_t) i], sig.right[(std::size_t) i]);
    CHECK (dec.locked() && std::abs (dec.position() - sig.place.back()) < 0.005);
}

TEST (dvs167_decoder_follows_the_record)
{
    using namespace lynx;
    for (const int st : { 1, 2, 3, 4, 5 })
    {
        dvs::warm (st, true);
        const auto& f = *dvs::format (st);
        for (const double speed : { 1.0, 0.5, 1.5, -1.0 })
        {
            const double rate = st == 2 ? 48000.0 : 44100.0;
            const auto sig = dvstest::make (f, 60.0, [speed] (double) { return speed; }, (int) rate * 2, rate);
            dvs::Decoder dec;
            dec.configure (st, rate);
            for (std::size_t i = 0; i < sig.left.size(); ++i)
                dec.process (sig.left[i], sig.right[i]);
            CHECK (std::abs (dec.pitch() - speed) < 0.005);
            CHECK (dec.forwards() == (speed > 0.0));
            // forwards the register names the place; backwards it lags by its own length (FL's reading); Serato CD's
            // taps miss the register's top bit, so FL's backwards rule never agrees: speed only (position from 0)
            if (speed > 0.0)
                CHECK (dec.locked() && std::abs (dec.position() - sig.place.back()) < 0.005);
            else if (st != 3)
                CHECK (dec.locked() && std::abs (dec.position() - sig.place.back()) < (f.bits + 1.0) / f.resolution);
            else
                CHECK (! dec.locked());
        }
    }
    // a scratch: forwards, a stop, backwards, forwards again; the decoder keeps up (within 30 ms) once locked
    const auto& f = *dvs::format (1);
    auto scratch = [] (double t) { return t < 1.0 ? 1.0 : t < 1.3 ? 1.0 - (t - 1.0) / 0.15 : t < 1.6 ? -1.0 : 1.0; };
    const auto sig = dvstest::make (f, 120.0, scratch, 44100 * 3, 44100.0);
    dvs::Decoder dec;
    dec.configure (1, 44100.0);
    double worst = 0.0;
    for (std::size_t i = 0; i < sig.left.size(); ++i)
    {
        dec.process (sig.left[i], sig.right[i]);
        if (i > 22050)
            worst = std::max (worst, std::abs (dec.position() - sig.place[i]));
    }
    CHECK (worst < 0.03);
    // the unsupported types decode nothing
    dvs::Decoder none;
    none.configure (6, 44100.0);
    CHECK (! none.active());
}

TEST (dvs167_scratcher_plays_the_record)
{
    using namespace lynx;
    // a sample whose frame k holds k / n (a ramp), so the output names the frame played
    const int n = 44100;
    std::vector<float> ramp ((std::size_t) n);
    for (int i = 0; i < n; ++i)
        ramp[(std::size_t) i] = (float) std::round ((double) i / n * 32767.0) / 32768.0f;
    auto table = scratcher::prepareSample (ramp.data(), ramp.data(), (std::size_t) n, 44100.0);
    CHECK (table != nullptr);
    scratcher::Engine eng;
    eng.setSample (table);
    eng.setSampleRate (44100.0);
    dvs::warm (1, true);
    eng.setTimecode (1, false);
    std::vector<float> zl (256, 0.0f), zr (256, 0.0f);
    eng.process (zl.data(), zr.data(), 256); // (the sample takes over at the end of the first block)

    // 2 s of record from 10.25 s: the output is the sample at the record's place (looping every second)
    const auto sig = dvstest::make (*dvs::format (1), 10.25, [] (double) { return 1.0; }, 44100 * 2, 44100.0);
    std::vector<float> l (sig.left), r (sig.right);
    for (std::size_t at = 0; at < l.size(); at += 256)
    {
        const auto m = (int) std::min<std::size_t> (256, l.size() - at);
        eng.process (l.data() + at, r.data() + at, m);
    }
    double worst = 0.0;
    for (std::size_t i = 44100; i < l.size(); i += 97)
    {
        const auto want = std::fmod (sig.place[i], 1.0);
        auto diff = std::abs ((double) l[i] - want);
        diff = std::min (diff, 1.0 - diff); // (the loop's seam)
        worst = std::max (worst, diff);
        CHECK (l[i] == r[i]); // the sample only: the timecode input is not passed
    }
    CHECK (worst < 0.003);
    CHECK (std::abs (eng.position() - std::fmod (sig.place.back(), 1.0)) < 0.003);

    // the needle lifts (silence): the place stops, the last frame holds 0.1 s then fades to nothing in ~0.3 s
    std::vector<float> sl (44100, 0.0f), sr (44100, 0.0f);
    for (int at = 0; at < 44100; at += 256)
        eng.process (sl.data() + at, sr.data() + at, std::min (256, 44100 - at));
    CHECK (std::abs (sl[100]) > 0.0f && sl[30000] == 0.0f);
}

TEST (dvs167_riaa_preamp)
{
    using namespace lynx;
    // FL's RIAA preamp [DECOMP 0x72cdf0]: y = 20 x − 20 z₁ x₋₁ + (p₁ + p₂) y₋₁ − p₁ p₂ y₋₂ (an impulse shows it)
    const double rate = 44100.0;
    const auto p1 = std::exp (-1.0 / (rate * 0.00318)), p2 = std::exp (-1.0 / (rate * 7.5e-5)), z1 = std::exp (-1.0 / (rate * 3.18e-4));
    scratcher::Engine eng;
    std::vector<float> ramp (1000, 0.0f);
    eng.setSample (scratcher::prepareSample (ramp.data(), ramp.data(), ramp.size(), 44100.0));
    eng.setSampleRate (rate);
    eng.setTimecode (6, true); // (a type LYNX does not decode: the preamp still runs, the platter stays silent)
    std::vector<float> l (64, 0.0f), r (64, 0.0f);
    eng.process (l.data(), r.data(), 64);
    std::vector<float> x (64, 0.0f), y (64, 0.0f);
    x[0] = 0.001f;
    eng.process (x.data(), y.data(), 64);
    CHECK (x[0] == 0.0f); // the output is the (silent) platter, never the input
    // the filter: RIAA playback, about +19.3 dB at 20 Hz and −19.6 dB at 20 kHz around 0 dB at 1 kHz
    const auto c = scratcher::riaaCoefficients (rate);
    CHECK (c[0] == 20.0 && std::abs (c[1] + 20.0 * z1) < 1e-12 && std::abs (c[2] + p1 + p2) < 1e-12 && std::abs (c[3] - p1 * p2) < 1e-12);
    auto gainDb = [&c, rate] (double hz) {
        const std::complex<double> z = std::polar (1.0, -2.0 * 3.141592653589793 * hz / rate);
        return 20.0 * std::log10 (std::abs ((c[0] + c[1] * z) / (1.0 + c[2] * z + c[3] * z * z)));
    };
    const auto ref = gainDb (1000.0);
    CHECK (std::abs (gainDb (20.0) - ref - 19.3) < 0.5 && gainDb (10000.0) - ref < -12.0);
}
