// Round 190u: FL Studio Mobile's instrument modules on LYNX's engines (F4, after MiniSynth): Transistor Bass.
// Uses the .flm builders of FlmImport182cTests.inc.

namespace flm190u
{
// master + one instrument rack (module `type` with `params` values k / 100) and its channel with one note
inline flm182c::Bytes engineProject (std::uint32_t type, int params)
{
    namespace t = flm182c;
    t::Bytes f;
    t::tag (f, "10LF");
    t::Bytes m;
    t::fixed (m, "2025-07-06 11-39-10", 256);
    t::f64 (m, 0);
    t::fixed (m, "", 256);
    t::fixed (m, "Engines", 256);
    t::u8 (m, 0);
    t::fixed (m, "", 2048);
    t::fixed (m, "2026-11-10 15:00:00", 256);
    const t::Bytes tdiv { 4, 4 };
    return t::cat ({ f, t::head ("Engines", 120.0), t::chunk ("META", m), t::chunk ("TDIV", tdiv), t::rack (lynx::flm::kindMaster, { t::module (28, 1) }),
                     t::rack (lynx::flm::kindNormal, { t::module (type, 2, {}, "Default/Default", 0, 127, params, true) }),
                     t::channel (lynx::flm::kindMaster, "MASTER", 0, {}),
                     t::channel (lynx::flm::kindNormal, "Bass", 1, { t::lane (0, "Bass", { t::clip (0, 4, 4, 0, 1, { { 0, 2, 60 } }) }) }) });
}

// the fundamental of the left channel in [from, to): the autocorrelation peak between 20 Hz and 2 kHz
inline double fundamental (const std::vector<float>& stereo, int from, int to, double rate)
{
    std::vector<double> x;
    for (int i = from; i < to; ++i)
        x.push_back ((double) stereo[2 * (std::size_t) i]);
    std::vector<double> ac;
    const auto first = (int) (rate / 2000.0);
    for (int k = first; k < (int) (rate / 20.0) && k < (int) x.size() / 2; ++k)
    {
        double a = 0.0;
        for (std::size_t i = 0; i + (std::size_t) k < x.size(); ++i)
            a += x[i] * x[i + (std::size_t) k];
        ac.push_back (a / (double) (x.size() - (std::size_t) k));
    }
    if (ac.empty())
        return 0.0;
    // the shortest lag among the peaks (every multiple of the period is one)
    const auto best = *std::max_element (ac.begin(), ac.end());
    for (std::size_t k = 1; k + 1 < ac.size(); ++k)
        if (ac[k] >= 0.9 * best && ac[k] >= ac[k - 1] && ac[k] >= ac[k + 1])
            return rate / (double) (first + (int) k);
    return 0.0;
}

inline float peakOf (const std::vector<float>& y, int from, int to)
{
    float p = 0.0f;
    for (int i = 2 * from; i < 2 * to && i < (int) y.size(); ++i)
        p = std::max (p, std::fabs (y[(std::size_t) i]));
    return p;
}
} // namespace flm190u

TEST (flm190u_transistor_bass_plays_flms_build)
{
    using namespace lynx;
    FlpImportReport report;
    std::string error;
    const auto song = importFlm (flm190u::engineProject (flm::module::transistorBass, 14), flm182c::options (&report), &error);
    CHECK (song.has_value());
    if (! song)
        return;
    const auto* bass = flm182c::channelNamed (*song, "Bass");
    CHECK (bass != nullptr && bass->instrument == InstrumentKind::transistorbass && transistorBassIsFlm (*bass) && bass->samplePath.empty());
    CHECK (report.converted.count ("FL Studio Mobile: Transistor Bass") == 1 && report.unsupported.count ("FL Studio Mobile: Transistor Bass") == 0);
    if (bass == nullptr)
        return;
    const auto& state = bass->pluginState;
    CHECK (transistorBassStateUsable (state) && transistorBassOversampling (state) == 1);
    // the equivalent desktop patch (spec of round 190n §4): FLM's k / 100 on the desktop knob with the same law, Drive
    // as its square root, FLM's fixed controls
    const auto value = [&state] (int j) {
        float v = -1.0f;
        if (const auto at = transistorBassParamOffset (state, j); at >= 0)
            std::memcpy (&v, state.data() + at, 4);
        return v;
    };
    const std::pair<int, float> expected[] = { { 0, 1.0f }, { 1, 0.01f }, { 2, 0.02f }, { 3, 0.0f }, { 4, 0.03f }, { 5, 0.04f }, { 6, 0.05f }, { 7, 0.06f },
                                               { 8, 1.0f / 3.0f }, { 9, 0.5f }, { 10, 0.5f }, { 11, 0.11f }, { 12, 0.12f }, { 13, 0.13f }, { 14, 0.0f },
                                               { 27, 0.07f }, { 28, 0.08f }, { 29, 0.3f }, { 30, 1.0f }, { 31, 0.10f }, { 32, 0.0f }, { 40, 0.0f }, { 41, 0.0f } };
    for (const auto& [j, v] : expected)
        CHECK (std::abs (value (j) - v) < 1e-6f);
    const auto program = transistorBassProgram (state);
    CHECK (program && ! program->sequencer);

    // FLM's default patch through the module's note layer; no file of an FL installation is involved
    const auto def = transistorBassFlmState ({ 1.0f, 0.0f, 1.0f, 1.0f, 0.1f, 1.0f, 0.1f, 0.0f, 0.0f, 0.3f, 0.5f, 0.0f, 0.0f, 0.0f });
    CHECK (transistorBassStateUsable (def));
    CHECK (transistorBassFlmState ({ 1.0f, 0.0f, 1.0f }).empty());
    tbass::RefOptions flmBuild;
    flmBuild.flm = true;
    const double rate = 44100.0;
    const auto play = [&] (std::vector<tbass::RefNote> notes, int length) { return tbass::renderReference (def, "", notes, rate, length, flmBuild); };
    const auto note = [] (int start, int key, double velocity, int release) {
        tbass::RefNote n;
        n.start = start, n.key = key, n.initVol = velocity, n.release = release;
        return n;
    };
    CHECK (tbass::renderReference (def, "", { note (0, 60, 0.6, 20000) }, rate, 30000).empty()); // the desktop plugin needs its DLL

    // key 60 sounds an octave below middle C (130.8 Hz), as FLM inside FL (A/B, round 190u)
    const auto plain = play ({ note (0, 60, 0.6, 20000) }, 40000);
    CHECK (plain.size() == 80000);
    if (plain.size() != 80000)
        return;
    const auto f0 = flm190u::fundamental (plain, 4000, 16000, rate);
    CHECK (std::abs (f0 - 130.81) < 1.0);
    // the engine runs a quarter second after the last key, then stands still: exact silence
    CHECK (flm190u::peakOf (plain, 20000 + 11025, 40000) == 0.0f && flm190u::peakOf (plain, 0, 20000) > 0.01f);

    // velocity: an accent from 99 / 127 on, and nothing else
    const auto v98 = play ({ note (0, 60, 98.0 / 127.0, 20000) }, 30000), v99 = play ({ note (0, 60, 99.0 / 127.0, 20000) }, 30000),
               v127 = play ({ note (0, 60, 1.0, 20000) }, 30000), v10 = play ({ note (0, 60, 10.0 / 127.0, 20000) }, 30000);
    CHECK (v98 == v10 && v99 == v127 && v98 != v99);
    CHECK (flm190u::peakOf (v99, 0, 20000) > 1.3f * flm190u::peakOf (v98, 0, 20000));

    // a note over a held one slides up without a new attack; releasing it slides back to the held key
    // (the slide is a 50 ms glide to 90 %: measured from 150 ms on)
    const auto legato = play ({ note (0, 48, 0.6, 36000), note (10000, 60, 0.6, 22000) }, 40000);
    CHECK (std::abs (flm190u::fundamental (legato, 3000, 9500, rate) - 65.41) < 0.6);
    CHECK (std::abs (flm190u::fundamental (legato, 16700, 21500, rate) - 130.81) < 1.0);
    CHECK (std::abs (flm190u::fundamental (legato, 28700, 35500, rate) - 65.41) < 0.6);
    // (the desktop rule would stop at the first release of the pair; here the lower key still sounds)
    CHECK (flm190u::peakOf (legato, 28700, 35500) > 0.005f);
    // releasing the lower key of a pair changes nothing
    const auto upper = play ({ note (0, 48, 0.6, 14000), note (10000, 60, 0.6, 30000) }, 34000);
    CHECK (std::abs (flm190u::fundamental (upper, 21000, 29500, rate) - 130.81) < 1.0);
    for (std::size_t i = 2 * 13000; i < 2 * 16000; ++i) // (the same samples as the pair held to the end)
        if (upper[i] != legato[i])
        {
            CHECK (upper[i] == legato[i]);
            break;
        }

    // note pan: a balance that creeps in (1 % per 512 samples) and returns to the centre between notes
    {
        auto first = note (0, 60, 0.6, 12000), second = note (30000, 60, 0.6, 60000);
        second.finalPan = 1.0;
        const auto z = play ({ first, second }, 60000);
        double l0 = 0.0, r0 = 0.0, l1 = 0.0, r1 = 0.0, l2 = 0.0, r2 = 0.0;
        for (int i = 2000; i < 10000; ++i)
            l0 += std::fabs (z[2 * (std::size_t) i]), r0 += std::fabs (z[2 * (std::size_t) i + 1]);
        for (int i = 30500; i < 34000; ++i)
            l1 += std::fabs (z[2 * (std::size_t) i]), r1 += std::fabs (z[2 * (std::size_t) i + 1]);
        for (int i = 56000; i < 59500; ++i)
            l2 += std::fabs (z[2 * (std::size_t) i]), r2 += std::fabs (z[2 * (std::size_t) i + 1]);
        CHECK (l0 == r0);                         // centre: both sides the same samples
        CHECK (l1 < r1 && l1 > 0.9 * r1);         // just after the note-on the left side has barely moved
        CHECK (l2 < 0.65 * r2 && l2 > 0.5 * r2);  // 26 000 samples later: 1 − 0.99^(26000/512) ≈ 40 % of the way to 0
    }

    // the instrument: a channel imported from a .flm plays without the DLL; the same state as a desktop channel doesn't
    const auto rendered = [] (const ChannelDef& c) {
        auto inst = makeTransistorBassInstrument();
        inst->prepare (44100.0);
        inst->configure (c, 120.0);
        inst->noteOn (60, 0.8f);
        std::vector<float> l (4096, 0.0f), r (4096, 0.0f);
        float peak = 0.0f;
        for (int b = 0; b < 4; ++b)
        {
            std::fill (l.begin(), l.end(), 0.0f), std::fill (r.begin(), r.end(), 0.0f);
            inst->render (l.data(), r.data(), 4096, 1.0f, 1.0f);
            for (const auto v : l)
                peak = std::max (peak, std::fabs (v));
        }
        inst->noteOff (60);
        for (int b = 0; b < 12 && ! inst->idle(); ++b)
            inst->render (l.data(), r.data(), 4096, 1.0f, 1.0f);
        return std::pair<float, bool> { peak, inst->idle() };
    };
    const auto mobile = rendered (*bass);
    CHECK (mobile.first > 1.0e-4f && mobile.second); // (the test patch is nearly closed)
    auto desktop = *bass;
    desktop.modelParams.clear();
    CHECK (! transistorBassIsFlm (desktop) && rendered (desktop).first == 0.0f);

    // end to end: the imported channel through the instrument is the module's own reference render, note after note
    // (the desktop's note pan gains were written over the module's balance: every note after the first came out 3 dB
    // down, rising)
    {
        auto inst = makeTransistorBassInstrument();
        inst->prepare (44100.0);
        inst->configure (*bass, 120.0);
        constexpr int length = 90112, block = 512;
        const int starts[] = { 0, 30208, 60416 }, keys[] = { 60, 48, 67 };
        const float velocities[] = { 0.5f, 0.9f, 0.5f };
        std::vector<float> l ((std::size_t) length, 0.0f), r ((std::size_t) length, 0.0f);
        std::vector<tbass::RefNote> notes;
        for (int k = 0; k < 3; ++k)
        {
            tbass::RefNote n;
            n.start = starts[k], n.key = keys[k], n.initVol = velocities[k], n.release = starts[k] + 8192;
            notes.push_back (n);
        }
        for (int pos = 0; pos < length; pos += block)
        {
            for (int k = 0; k < 3; ++k)
            {
                if (pos == starts[k])
                    inst->noteOn (keys[k], velocities[k]);
                if (pos == starts[k] + 8192)
                    inst->noteOff (keys[k]);
            }
            inst->render (l.data() + pos, r.data() + pos, block, 1.0f, 1.0f);
        }
        tbass::RefOptions o;
        o.flm = true;
        const auto ref = tbass::renderReference (bass->pluginState, "", notes, 44100.0, length, o);
        CHECK ((int) ref.size() == 2 * length);
        double worst = 0.0, energy = 0.0;
        if ((int) ref.size() == 2 * length)
            for (int i = 0; i < length; ++i)
            {
                worst = std::max ({ worst, std::abs ((double) l[(std::size_t) i] - ref[2 * (std::size_t) i]), std::abs ((double) r[(std::size_t) i] - ref[2 * (std::size_t) i + 1]) });
                if (i >= starts[1])
                    energy += (double) ref[2 * (std::size_t) i] * ref[2 * (std::size_t) i];
            }
        CHECK (energy > 1.0e-6 && worst < 1.0e-6);
    }
}

// The desktop plugin plays at 4× whatever the state's oversampling field says (A/B inside FL, round 190u: FL's render
// of a state saved with 1 is this engine at 4×). Needs the installation's engine DLL.
TEST (flm190u_transistor_bass_desktop_ignores_the_oversampling_field)
{
    using namespace lynx;
    const auto dll = tbassDll();
    if (dll.empty() || ! transistorBassDllUsable (dll))
        return;
    std::array<float, 42> patch {};
    patch[0] = 0.5f, patch[2] = 0.6f, patch[4] = 0.7f, patch[5] = 0.5f, patch[6] = 0.5f, patch[8] = 0.5f, patch[9] = 0.5f;
    const auto s4 = transistorBassWriteState (patch, 4), s1 = transistorBassWriteState (patch, 1);
    CHECK (transistorBassStateUsable (s4) && transistorBassOversampling (s4) == 4 && transistorBassOversampling (s1) == 1);
    tbass::RefNote n;
    n.start = 0, n.key = 48, n.initVol = 1.0, n.release = 12000;
    const auto a = tbass::renderReference (s4, dll, { n }, 44100.0, 16000), b = tbass::renderReference (s1, dll, { n }, 44100.0, 16000);
    CHECK (! a.empty() && a == b);
    // FLM's build of the same state is the 1× engine: not the same samples
    tbass::RefOptions flmBuild;
    flmBuild.flm = true;
    n.initVol = 0.5;
    CHECK (tbass::renderReference (s1, dll, { n }, 44100.0, 16000, flmBuild) != tbass::renderReference (s4, dll, { n }, 44100.0, 16000));
}
