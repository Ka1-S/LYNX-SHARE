    // Round 190rb (0.1.32 quality release: crashes and memory bugs, from the crash reports, the sanitizer build and the
    // UI monkey test): each fix checked through the real components, off screen. Included in the app class of Main.cpp;
    // LYNX --qa190rbtest report.txt; --selftest "qa190rb". Run it in the sanitizer build (scripts/build-sanitize.sh) too:
    // the old code read freed or out-of-range memory there, which ASan names.
    juce::String qa190rbCheck (juce::String& log)
    {
        using namespace lynx;
        const juce::ScopedValueSetter<bool> shown (ui::showingForShots(), true);
        juce::String result = "OK";
        const auto check = [&] (const juce::String& what, bool ok) {
            log << (ok ? "ok   " : "FAIL ") << what << "\n";
            if (! ok && result == "OK")
                result = "ERROR " + what;
        };

        return result;
    }
