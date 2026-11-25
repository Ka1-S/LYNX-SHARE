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

