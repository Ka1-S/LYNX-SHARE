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
