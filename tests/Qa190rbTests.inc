// Round 190rb (0.1.32 quality release): crashes and memory bugs found in the crash reports since 0.1.29, the sanitizer
// build (scripts/build-sanitize.sh: ASan + UBSan) and the UI monkey test (tools/ui_monkey.py). The UI fixes are checked
// by --qa190rbtest / --selftest "qa190rb" (src/Qa190rbCheck.inc); the core ones here.

