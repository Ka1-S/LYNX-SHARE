// Round 190rb (0.1.32 quality release): crashes and memory bugs found in the crash reports since 0.1.29, the sanitizer
// build (scripts/build-sanitize.sh: ASan + UBSan) and the UI monkey test (tools/ui_monkey.py). The UI fixes are checked
// by --qa190rbtest / --selftest "qa190rb" (src/Qa190rbCheck.inc); the core ones here.

// crash report 36351db8 (0.1.29, Windows): integer division by zero in Signalsmith's STFT configure, a voice pitch
// shifter made at a sample rate of 0 (ShifterStock::topUp while the rack was built before a device was open)
TEST (qa190rb_voice_shifter_at_an_unusable_rate_does_not_divide_by_zero)
{
    for (const auto rate : { 0.0, -1.0, 1.0, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity() })
    {
        lynx::VoicePitchShifter shifter;
        shifter.allocate (rate, lynx::StretchMethod::general);
        CHECK (shifter.ready());
        CHECK (shifter.primeLength (1.0) > 0);
        std::vector<float> in (8192, 0.25f), outL (128), outR (128);
        shifter.start (in.data(), in.data(), shifter.primeLength (1.0));
        shifter.process (in.data(), in.data(), 128, outL.data(), outR.data(), 128);
        CHECK (std::all_of (outL.begin(), outL.end(), [] (float v) { return std::isfinite (v); }));
    }
    // a real rate is unchanged (the same priming as before the guard)
    lynx::VoicePitchShifter a, b;
    a.allocate (48000.0, lynx::StretchMethod::general);
    b.allocate (48000.0, lynx::StretchMethod::general);
    CHECK (a.primeLength (1.0) == b.primeLength (1.0));
}

// crash report b230f196 (0.1.30, Windows): Helix's sample oscillator with Clean (wt::Engine sampleStepClean) on the audio
// thread. A voice started with Clean keeps it, but a new sample reaches the engine without its band-limited levels until
// the UI thread has made them (setSampleLevels (nullptr) meanwhile, HelixInstrument::take): the read dereferenced them
TEST (qa190rb_helix_clean_sample_voice_survives_a_sample_without_levels)
{
    namespace w = lynx::wt;
    w::HelixUserData d;
    d.sample = helix190mt::saw (225.0, 1.0);
    auto s = helix190mt::patch();
    s[(std::size_t) (w::ext2Base + w::ext2::sampleOn)] = 1.0;
    s[(std::size_t) (w::ext2Base + w::ext2::sampleRoot)] = 57.0;
    helix190mt::x3 (s, w::ext3::sampleClean, 1.0);
    const auto levels = w::sampleLevels (d.sample);
    w::Engine e;
    e.prepare (48000.0);
    e.setSample (&d.sample);
    e.setSampleLevels (levels.get());
    e.setState (s.data());
    std::vector<float> l (9600, 0.0f), r (9600, 0.0f);
    e.note (57 + 19, 1.0f); // (well above the root: Clean reads a level)
    e.render (l.data(), r.data(), 2400, 120.0, -1.0);
    // a new sample arrives (another file loaded while the note sounds): no levels yet
    w::HelixUserData other;
    other.sample = helix190mt::saw (300.0, 0.5);
    e.setSample (&other.sample);
    e.setSampleLevels (nullptr);
    e.render (l.data() + 2400, r.data() + 2400, 2400, 120.0, -1.0);
    // its levels come a moment later
    const auto otherLevels = w::sampleLevels (other.sample);
    e.setSampleLevels (otherLevels.get());
    e.render (l.data() + 4800, r.data() + 4800, 4800, 120.0, -1.0);
    CHECK (std::all_of (l.begin(), l.end(), [] (float v) { return std::isfinite (v) && std::fabs (v) < 4.0f; }));
    CHECK (std::any_of (l.begin() + 2400, l.begin() + 4800, [] (float v) { return v != 0.0f; })); // (the voice went on)
}


// Round 190rf: crash reports 0.1.30 Linux (seven, three users: wt::Engine::Impl::grainStep / sampleStepClean on the audio
// thread) and Windows b230f196 (sampleStepClean): the engine's sample went to none (a preset without sample data applied,
// HelixBank) while a voice played it. A voice decides "sample / grains / Clean" at its control steps, every 16 samples and
// across render calls, so between two of them it read *sample through a null pointer. Renders of odd lengths keep a
// control step open across the change; with the sample (plain, Clean) and with grains.
TEST (qa190rf_helix_voice_survives_its_sample_going_away)
{
    namespace w = lynx::wt;
    for (const int mode : { 0, 1, 2 }) // the sample read, the sample with Clean, round 3's grains
    {
        w::HelixUserData d;
        d.sample = helix190mt::saw (225.0, 1.0);
        auto s = helix190mt::patch();
        s[(std::size_t) (w::ext2Base + w::ext2::sampleOn)] = mode < 2 ? 1.0 : 0.0;
        s[(std::size_t) (w::ext2Base + w::ext2::sampleRoot)] = 57.0;
        if (mode == 1)
            helix190mt::x3 (s, w::ext3::sampleClean, 1.0);
        if (mode == 2)
        {
            helix190mt::x3 (s, w::ext3::grainOn, 1.0);
            helix190mt::x3 (s, w::ext3::grainDensity, 1.0);
        }
        const auto levels = w::sampleLevels (d.sample);
        w::Engine e;
        e.prepare (48000.0);
        e.setSample (&d.sample);
        e.setSampleLevels (levels.get());
        e.setState (s.data());
        std::vector<float> l (12000, 0.0f), r (12000, 0.0f);
        e.note (57 + 19, 1.0f);
        e.render (l.data(), r.data(), 2407, 120.0, -1.0); // (2407: the voice's control step is half done at the change)
        e.setSample (nullptr);
        e.setSampleLevels (nullptr);
        e.render (l.data() + 2407, r.data() + 2407, 2401, 120.0, -1.0);
        // a sample again (the next preset brings its own): the voice plays it
        w::HelixUserData other;
        other.sample = helix190mt::saw (300.0, 0.5);
        e.setSample (&other.sample);
        e.render (l.data() + 4808, r.data() + 4808, 7192, 120.0, -1.0);
        CHECK (std::all_of (l.begin(), l.end(), [] (float v) { return std::isfinite (v) && std::fabs (v) < 4.0f; }));
    }
}

// LeakSanitizer in the sanitizer suite (round 190rb; ctl170i3_every_control_changes_the_render): a Fruit Kick channel's
// instrument never freed the engine it was playing (~300 KB each time a rack dropped or rebuilt the instrument: a project
// opened or closed, the channel deleted). Measured here with glibc's heap counters; the sanitizer build's LSan reports it
// on its own (no heap counters there).
#if defined(__GLIBC__) && ! defined(LYNX_SANITIZER_BUILD)
 #include <malloc.h>
TEST (qa190rb_fruit_kick_instrument_frees_its_engine)
{
    using namespace lynx;
    const auto kick = bankstates::channelFor ("fruitkick", { 100, -500, 70, 130, 20, 5 });
    CHECK (kick.has_value());
    if (! kick)
        return;
    std::vector<float> l (512), r (512);
    const auto cycle = [&] {
        auto inst = makeInstrument (InstrumentKind::fruitkick);
        inst->prepare (48000.0);
        inst->configure (*kick, 120.0);
        inst->noteOn (60, 0.8f);
        inst->render (l.data(), r.data(), 512, 1.0f, 1.0f); // (the engine is taken here)
    };
    cycle(); // (first-time caches)
    const auto before = mallinfo2().uordblks;
    for (int i = 0; i < 40; ++i)
        cycle();
    const auto grown = (long long) mallinfo2().uordblks - (long long) before;
    CHECK (grown < 1024 * 1024); // (the leak was ~12 MB for 40 instruments)
}
#endif

// Round 190rf (ThreadSanitizer: WorkerPool's `target` read after the round could have ended): a worker switched out
// between its last count and its wake took the flag of the caller's next wait, and parallelFor returned with indices
// still running. The pool holds its workers up there; every round must be complete when it returns.
TEST (qa190rf_worker_pool_never_returns_before_its_round_is_done)
{
    lynx::WorkerPool pool (3);
    pool.setStallForTest (120);
    std::atomic<int> finished { 0 }, outside { 0 };
    std::atomic<bool> open { false };
    int early = 0;
    for (int r = 0; r < 6000; ++r)
    {
        finished.store (0);
        open.store (true);
        auto fn = [&, r] (int i) {
            if (! open.load())
                outside.fetch_add (1);
            const auto until = std::chrono::steady_clock::now() + std::chrono::microseconds (i == 0 ? 5 : (r & 3) == 3 ? 120 : 3);
            while (std::chrono::steady_clock::now() < until) {}
            finished.fetch_add (1);
        };
        pool.parallelFor (4, fn);
        if (finished.load() != 4)
        {
            ++early;
            while (finished.load() != 4) // (the lambda must outlive its indices)
                std::this_thread::yield();
        }
        open.store (false);
    }
    CHECK (early == 0);
    CHECK (outside.load() == 0);
}

// Round 190rf (ThreadSanitizer): the EQ's latency is asked by the UI thread (the plan of every new snapshot) while the
// audio thread switches FL's linear-phase mode, which frees the linear-phase engine: it is a published number now. The
// sanitizer builds name the old read of the freed engine; here: the numbers are the two modes' own.
TEST (qa190rf_parametric_eq_latency_is_asked_while_the_mode_switches)
{
    using namespace lynx;
    auto params = makeEffectSlot (EffectKind::parametricEq).params;
    params[(std::size_t) peqfl::lawParam] = 0.5f; // FL's Parametric EQ 2
    auto linear = params;
    linear[(std::size_t) peqfl::linearParam] = 1.0f;
    auto eq = makeEffect (EffectKind::parametricEq);
    eq->prepare (48000.0);
    std::vector<float> l (64, 0.1f), r (64, 0.1f);
    eq->process (l.data(), r.data(), 64, linear, 120.0);
    const auto with = eq->latencySamples();
    eq->process (l.data(), r.data(), 64, params, 120.0);
    const auto without = eq->latencySamples();
    CHECK (with > 0);
    CHECK (without == 0);
    std::atomic<bool> stop { false };
    std::atomic<int> others { 0 };
    std::thread ui ([&] {
        while (! stop.load())
            if (const auto now = eq->latencySamples(); now != with && now != without)
                others.fetch_add (1);
    });
    for (int k = 0; k < 400; ++k)
        eq->process (l.data(), r.data(), 64, (k & 1) != 0 ? linear : params, 120.0);
    stop.store (true);
    ui.join();
    CHECK (others.load() == 0);
}
