// Round 190u: FL Studio Mobile's instrument modules on LYNX's engines (F4, after MiniSynth): Transistor Bass.
// Uses the .flm builders of FlmImport182cTests.inc.

namespace flm190u
{
// master + one instrument rack (module `type` with `params` values k / 100, or `values`) and its channel with one note
inline flm182c::Bytes engineProject (std::uint32_t type, int params, const std::vector<float>& values = {})
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
    auto bytes = t::cat ({ f, t::head ("Engines", 120.0), t::chunk ("META", m), t::chunk ("TDIV", tdiv), t::rack (lynx::flm::kindMaster, { t::module (28, 1) }),
                           t::rack (lynx::flm::kindNormal, { t::module (type, 2, {}, "Default/Default", 0, 127, params, true) }),
                           t::channel (lynx::flm::kindMaster, "MASTER", 0, {}),
                           t::channel (lynx::flm::kindNormal, "Bass", 1, { t::lane (0, "Bass", { t::clip (0, 4, 4, 0, 1, { { 0, 2, 60 } }) }) }) });
    if ((int) values.size() == params) // the module's PRMS chunk is the only one of this size
    {
        const auto size = (std::uint32_t) (4 * params);
        for (std::size_t i = 0; i + 8 + size <= bytes.size(); ++i)
            if (std::memcmp (bytes.data() + i, "PRMS", 4) == 0 && std::memcmp (bytes.data() + i + 4, &size, 4) == 0)
                std::memcpy (bytes.data() + i + 8, values.data(), size);
    }
    return bytes;
}

// FLM's 3x Osc defaults (the module's default table; equal to the untouched values of FLM's own templates)
inline std::vector<float> threeOscDefaults()
{
    std::vector<float> d (149, 0.0f);
    const auto fill = [&d] (int from, int to, float v) { std::fill (d.begin() + from, d.begin() + to + 1, v); };
    d[0] = 1.0f;
    fill (7, 12, 0.5f), d[13] = 0.5f, d[14] = 0.25f, fill (16, 21, 0.5f), d[22] = 0.5f, d[23] = 0.25f;
    fill (47, 51, 0.305f), fill (52, 61, 0.5f), fill (74, 77, 0.01f), fill (78, 81, 0.5f), fill (86, 89, 0.01f), fill (90, 93, 0.5f);
    fill (94, 97, 1.0f), fill (98, 101, 0.01f), d[102] = 0.105f, fill (103, 105, 0.5f), d[106] = 1.0f, fill (107, 109, 0.5f);
    d[111] = 1.0f, fill (113, 116, 0.5f), d[118] = 0.494f, fill (121, 124, 0.5f), d[125] = 0.833f, fill (126, 128, 0.5f), d[129] = 1.0f;
    d[130] = 0.333f, d[132] = 0.375f, fill (133, 136, 0.5f), d[140] = 0.576f, d[141] = 1.0f, d[142] = 0.167f, d[145] = 1.0f / 90.0f;
    return d;
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

// FLM's 3x Osc on LYNX's 3x Osc engine and FL's channel settings: every parameter back to the raw value FL keeps
TEST (flm190u_three_osc_maps_the_parameters)
{
    using namespace lynx;
    const auto import = [] (const std::vector<float>& values, FlpImportReport& report) {
        std::string error;
        auto song = importFlm (flm190u::engineProject (flm::module::threeOsc, 149, values), flm182c::options (&report), &error);
        return song;
    };
    const auto snap = [] (const ChannelDef& c, int record, int field) {
        std::int32_t v = 0;
        std::memcpy (&v, c.pluginState.data() + 0x60 + 0x44 * (std::size_t) record + 4 * (std::size_t) field, 4);
        return v;
    };
    const auto word = [] (const ChannelDef& c, std::size_t at) {
        std::int32_t v = 0;
        std::memcpy (&v, c.pluginState.data() + 0x60 + at, 4);
        return v;
    };

    // the defaults are the desktop plugin's defaults
    {
        FlpImportReport report;
        const auto song = import (flm190u::threeOscDefaults(), report);
        CHECK (song.has_value());
        if (! song)
            return;
        const auto* c = flm182c::channelNamed (*song, "Bass");
        CHECK (c != nullptr && c->instrument == InstrumentKind::threeosc && threeOscIsFlm (*c) && threeOscStateUsable (c->pluginState));
        CHECK (report.converted.count ("FL Studio Mobile: 3x Osc") == 1 && report.unsupported.empty());
        if (c == nullptr)
            return;
        const int expected[21] = { 0, 0, 0, 0, 0, 0, 64, 0, 0, -12, 0, 0, 0, 32, 0, 0, -24, 0, 0, 0, 0 };
        for (int i = 0; i < 21; ++i)
            CHECK (threeOscParam (c->pluginState, i) == expected[i]);
        for (int i = 21; i < 26; ++i)
            CHECK (threeOscParam (c->pluginState, i) == 0);
        // the volume record: envelope off, the desktop's release tension −101, sustain 128; LFO attack / speed near
        // the desktop's 20000 / 32950 (FLM's 0.305 / 0.5)
        CHECK (snap (*c, 1, 0) == 4 && snap (*c, 1, 1) == 0 && snap (*c, 1, 6) == 128 && snap (*c, 1, 16) == -101 && snap (*c, 1, 8) == 0);
        CHECK (snap (*c, 1, 10) == 19988 && snap (*c, 1, 12) == 32768 && snap (*c, 4, 11) == 0 && snap (*c, 0, 1) == 0);
        CHECK (word (*c, 0x15c) == 256 && word (*c, 0x160) == 0 && word (*c, 0x168) == 0);
        CHECK (c->modelParams.size() == 3 && c->modelParams[0] == 96.0f && c->pan == 0.0f && c->maxPolyphony == 0);
        // FLM sounds an octave below the desktop plugin (A/B inside FL): the channel's pitch, in semitones
        CHECK (c->sampler.pitch == -12.0f && c->modelParams[1] == 1.0f && c->modelParams[2] == 1.0f);
        CHECK (! c->misc.mono && ! c->misc.porta && c->misc.gateBeats == 0.0f && ! c->misc.echo.enabled() && ! c->arp.enabled());
        CHECK (std::abs (c->misc.portaBeats - 0.125f) < 1e-6f && c->misc.echo.echoes == 4 && std::abs (c->misc.echo.timeBeats - 0.75f) < 1e-6f);
        CHECK (c->misc.velocity.mid == 100 && c->misc.key.mid == 60);
    }

    // one value of every group
    {
        auto p = flm190u::threeOscDefaults();
        p[1] = 3.0f / 6.0f, p[2] = 2.0f / 6.0f, p[3] = 4.0f / 6.0f;      // saw, square, analog
        p[6] = 1.0f;                                                       // invert osc 3
        p[8] = 0.75f, p[11] = 0.8f, p[13] = 0.75f, p[16] = 0.6f, p[19] = 0.25f, p[22] = 0.4f, p[23] = 0.3f, p[24] = 1.0f, p[25] = 1.0f, p[26] = 0.5f;
        p[27 + 2] = 1.0f, p[32 + 4] = 1.0f, p[37 + 2] = 0.5f, p[42 + 2] = 0.1f, p[52 + 0] = 0.75f, p[52 + 2] = 0.8f, p[52 + 4] = 0.52f, p[57 + 4] = 0.45f;
        p[62] = 1.0f, p[66] = 1.0f, p[70] = 0.05f, p[74] = 0.25f, p[78] = 0.7f, p[82] = 0.1f, p[86] = 0.4f, p[90] = 0.3f, p[94] = 0.5f, p[98] = 0.35f, p[102] = 0.6f;
        p[63] = 1.0f, p[75] = 0.0f, p[107] = 0.85f;                        // cutoff envelope from its top
        p[110] = 6.0f / 7.0f, p[111] = 0.5f, p[112] = 0.7f, p[115] = 0.6f, p[116] = 0.4f;
        p[113] = 0.7f, p[114] = 0.35f, p[117] = 8.0f / 99.0f, p[118] = 0.6f, p[119] = 1.0f, p[120] = 1.0f;
        p[121] = 0.4f, p[122] = 0.5625f, p[125] = 0.5f, p[126] = 0.4375f, p[129] = 0.5f;
        p[130] = 6.0f / 9.0f, p[131] = 0.25f, p[132] = 0.25f, p[133] = 0.6f, p[134] = 0.5f + 2.0f / 24.0f, p[137] = 1.0f;
        p[139] = 4.0f / 5.0f, p[140] = 0.3f, p[141] = 0.6f, p[142] = 2.0f / 6.0f, p[143] = 2.0f / 5.0f, p[144] = 1.0f, p[145] = 2.0f / 90.0f;
        FlpImportReport report;
        const auto song = import (p, report);
        const auto* c = song ? flm182c::channelNamed (*song, "Bass") : nullptr;
        CHECK (c != nullptr);
        if (c == nullptr)
            return;
        const auto& st = c->pluginState;
        CHECK (threeOscParam (st, 1) == 3 && threeOscParam (st, 8) == 2 && threeOscParam (st, 15) == 4 && threeOscParam (st, 23) == 1 && threeOscParam (st, 21) == 0);
        CHECK (threeOscParam (st, 7 + 4) == 32 && threeOscParam (st, 7 + 5) == 30 && threeOscParam (st, 2) == 12 && threeOscParam (st, 3) == 20
               && threeOscParam (st, 0) == -32 && threeOscParam (st, 6) == 51 && threeOscParam (st, 13) == 38);
        CHECK (threeOscParam (st, 24) == 1 && threeOscParam (st, 25) == 1 && threeOscParam (st, 20) == 32);
        // LFOs: Mod X global + triangle + predelay, pitch tempo-synced, amounts in each record's own unit
        CHECK ((snap (*c, 2, 0) & 0x20) != 0 && snap (*c, 2, 13) == 1 && snap (*c, 2, 9) == 6554 && (snap (*c, 4, 0) & 2) != 0);
        CHECK (snap (*c, 0, 11) == 32 && snap (*c, 2, 11) == 77 && snap (*c, 4, 11) == 96 && snap (*c, 4, 12) == 29491);
        // the volume envelope (tempo-synced) and the cutoff envelope starting at its top (attack raw 100)
        CHECK (snap (*c, 1, 1) == 1 && (snap (*c, 1, 0) & 5) == 5 && snap (*c, 1, 2) == 3277 && snap (*c, 1, 3) == 16384 && snap (*c, 1, 4) == 6554
               && snap (*c, 1, 5) == 26214 && snap (*c, 1, 6) == 64 && snap (*c, 1, 7) == 22938);
        CHECK (snap (*c, 1, 14) == 51 && snap (*c, 1, 15) == -51 && snap (*c, 1, 16) == 26);
        CHECK (snap (*c, 2, 1) == 1 && snap (*c, 2, 3) == 100 && snap (*c, 2, 8) == 90);
        // the filter with the offsets folded in: 0.5 + 0.2, 0.7 − 0.2
        CHECK (word (*c, 0x168) == 6 && word (*c, 0x15c) == 179 && word (*c, 0x160) == 128);
        // the channel
        CHECK (std::abs (c->pan - 0.4f) < 1e-6f && c->modelParams.size() == 3 && c->maxPolyphony == 8);
        CHECK (c->misc.mono && c->misc.porta && c->synth.porta && c->misc.portaBeats > 0.125f && c->misc.gateBeats > 0.0f && c->misc.gateBeats < 1.0f);
        CHECK (c->misc.key.mid == 72 && std::abs (c->misc.key.pan - 0.5f) < 1e-6f && c->misc.velocity.mid == 60 && std::abs (c->misc.velocity.pan + 0.5f) < 1e-6f);
        CHECK (c->misc.echo.echoes == 7 && std::abs (c->misc.echo.feedback - 0.5f) < 1e-6f && std::abs (c->misc.echo.timeBeats - 0.5f) < 1e-6f
               && std::abs (c->misc.echo.pan - 0.4f) < 1e-5f && std::abs (c->misc.echo.pitch - 200.0f) < 1e-3f && c->misc.echo.pingPong && ! c->misc.echo.fat);
        CHECK (c->arp.direction == 5 && c->arp.range == 2 && std::abs (c->arp.gate - 0.6f) < 1e-6f && c->arp.octaveRepeat == 3 && c->arp.slide
               && c->arp.chord == std::vector<int> { 0 } && c->arp.chordSustain && c->arp.stepBeats > 0.0f && c->arp.stepBeats < 0.25f);
        CHECK (report.unsupported.empty());

        // what LYNX lacks is listed: Free Run
        p[146] = 1.0f;
        FlpImportReport listed;
        CHECK (import (p, listed).has_value() && listed.unsupported.size() == 1);
    }

    // a channel from a .flm: the level is linear in the velocity (the desktop's Init.Vol curve is not applied)
    {
        FlpImportReport report;
        const auto song = import (flm190u::threeOscDefaults(), report);
        const auto* c = song ? flm182c::channelNamed (*song, "Bass") : nullptr;
        if (c == nullptr)
            return;
        const auto peak = [] (const ChannelDef& channel, float velocity) {
            auto inst = makeThreeOscInstrument();
            inst->prepare (44100.0);
            inst->configure (channel, 120.0);
            inst->noteOn (60, velocity);
            std::vector<float> l (8192, 0.0f), r (8192, 0.0f);
            inst->render (l.data(), r.data(), 8192, 1.0f, 1.0f);
            float p = 0.0f;
            for (int i = 2048; i < 8192; ++i)
                p = std::max (p, std::fabs (l[(std::size_t) i]));
            return p;
        };
        const auto half = peak (*c, 0.5f), full = peak (*c, 1.0f);
        CHECK (half > 0.01f && std::abs (full / half - 2.0f) < 0.01f);
        auto desktop = *c;
        desktop.modelParams = { 128.0f };
        CHECK (! threeOscIsFlm (desktop) && peak (desktop, 1.0f) / peak (desktop, 0.5f) > 4.0f);
        // FLM's Volume multiplier is a plain gain (×2v)
        auto louder = *c;
        louder.modelParams[2] *= 1.5f;
        CHECK (std::abs (peak (louder, 0.5f) / half - 1.5f) < 0.01f);
    }
}

// FLM's envelope clock and stepping (round 190ab; the volume envelope of A/B setting E1 inside FL): the module's
// ticks are 384 / (the song's steps per beat) per beat, 96 for TDIV 4/4, in whole samples; the hold and the predelay
// last one tick more than the desktop's and the attack's position is not clamped, so an attack of 57 ticks (58 float
// steps of 1/58 stop just below 1) takes a 59th step to 1.0077, holds there for 13 ticks and the decay starts above 1
TEST (flm190ab_three_osc_envelope_steps_as_flm)
{
    using namespace lynx;
    // the articulator alone: E1's volume record at PPQ 96 and 120 BPM
    {
        const std::int32_t r[17] = { 4, 1, 0, 16384, 6554, 26214, 64, 22938, 0, 100, 20000, 0, 32950, 0, 51, -51, 26 };
        const auto run = [&r] (bool mobile, int& top, float& topValue, int& peak, float& peakValue, int& below) {
            flvp::Rec rec;
            rec.build (r, 96, 120.0);
            rec.mobile = mobile;
            CHECK (rec.hold == 12 && std::abs (1.0f / rec.att - 58.0f) < 1e-3f && std::abs (-1.0f / rec.dec - 184.0f) < 1e-2f);
            flvp::Art art;
            art.init (rec);
            top = peak = below = 0, peakValue = topValue = 0.0f;
            for (int step = 1; step <= 200; ++step)
            {
                art.step (rec);
                if (top == 0 && art.val >= 1.0f)
                    top = step, topValue = art.val;
                if (art.val > peakValue)
                    peak = step, peakValue = art.val;
                if (top != 0 && below == 0 && art.val < 1.0f)
                    below = step;
            }
            CHECK (art.state == 4 && art.val == 0.5f);
        };
        int top = 0, peak = 0, below = 0;
        float topValue = 0.0f, peakValue = 0.0f;
        run (true, top, topValue, peak, peakValue, below);
        CHECK (top == 59 && std::abs (topValue - 1.0076666f) < 2e-5f && peak == 73 && std::abs (peakValue - 1.0223727f) < 2e-5f && below == 76);
        run (false, top, topValue, peak, peakValue, below); // the desktop: clamped, the hold one tick shorter
        CHECK (top == 59 && topValue == 1.0f && peakValue == 1.0f && below == 72);

        // the predelay: one tick more
        std::int32_t d[17];
        std::copy (std::begin (r), std::end (r), d);
        d[2] = 6554;
        for (const auto mobile : { false, true })
        {
            flvp::Rec rec;
            rec.build (d, 96, 120.0);
            rec.mobile = mobile;
            flvp::Art art;
            art.init (rec);
            int first = 0;
            for (int step = 1; step <= 40 && first == 0; ++step)
            {
                art.step (rec);
                if (art.val > 0.0f)
                    first = step;
            }
            CHECK (rec.delay == 12 && first == (mobile ? 14 : 13));
        }
    }

    // the import: the tick rate from TDIV's second byte, and the instrument's whole-sample ticks
    {
        auto p = flm190u::threeOscDefaults();
        p[22] = 0.0f, p[23] = 0.0f;                                // oscillator 1 alone (a sine)
        p[62] = 1.0f, p[74] = 0.25f, p[78] = 0.7f, p[82] = 0.1f;   // E1: attack 25 %, tension +40 %, hold 10 %
        p[86] = 0.4f, p[90] = 0.3f, p[94] = 0.5f, p[98] = 0.35f, p[102] = 0.6f;
        const auto import = [&p] (int steps) {
            auto bytes = flm190u::engineProject (flm::module::threeOsc, 149, p);
            for (std::size_t i = 0; i + 10 <= bytes.size(); ++i)
                if (std::memcmp (bytes.data() + i, "TDIV", 4) == 0)
                    bytes[i + 9] = (std::uint8_t) steps;
            FlpImportReport report;
            std::string error;
            return importFlm (bytes, flm182c::options (&report), &error);
        };
        const auto three = import (3);
        const auto* c3 = three ? flm182c::channelNamed (*three, "Bass") : nullptr;
        CHECK (c3 != nullptr && c3->modelParams.size() == 3 && c3->modelParams[0] == 128.0f);
        const auto song = import (4);
        const auto* c = song ? flm182c::channelNamed (*song, "Bass") : nullptr;
        CHECK (c != nullptr && c->modelParams.size() == 3 && c->modelParams[0] == 96.0f);
        if (c == nullptr)
            return;
        // the level per 128 samples of a held key 84 (523 Hz in FLM's octave, 84 samples a cycle)
        const auto levels = [] (const ChannelDef& channel) {
            auto inst = makeThreeOscInstrument();
            inst->prepare (44100.0);
            inst->configure (channel, 120.0);
            inst->noteOn (84, 1.0f);
            const int n = 24000;
            std::vector<float> l ((std::size_t) n, 0.0f), r ((std::size_t) n, 0.0f), out;
            inst->render (l.data(), r.data(), n, 1.0f, 1.0f);
            for (int a = 0; a + 128 <= n; a += 128)
                out.push_back (*std::max_element (l.begin() + a, l.begin() + a + 128, [] (float x, float y) { return std::fabs (x) < std::fabs (y); }));
            for (auto& v : out)
                v = std::fabs (v);
            return out;
        };
        const auto stats = [] (const std::vector<float>& e, float& hold, float& bump, int& falls) {
            hold = *std::max_element (e.begin() + 112, e.begin() + 124);   // samples 14 336 … 15 872: the hold
            bump = *std::max_element (e.begin() + 126, e.begin() + 140);   // 16 128 … 17 920: the decay's start
            falls = 0;
            for (int k = 112; k < (int) e.size() && falls == 0; ++k)
                if (e[(std::size_t) k] < 0.95f * hold)
                    falls = 128 * k;
        };
        float hold = 0.0f, bump = 0.0f;
        int falls = 0;
        stats (levels (*c), hold, bump, falls);
        // FLM: 59 ticks of 230 samples to the top, 13 of hold, the decay from 1.0224 / 1.0077 of the hold's level
        CHECK (hold > 0.05f && std::abs (bump / hold - 1.0223727f / 1.0076666f) < 0.004f);
        const auto flmFalls = falls;
        auto desktop = *c;
        desktop.modelParams = { 96.0f };
        stats (levels (desktop), hold, bump, falls);
        CHECK (std::abs (bump / hold - 1.0f) < 0.003f && flmFalls - falls >= 600 && flmFalls - falls <= 1300); // 4 ticks of 230, in steps of 128
    }
}
