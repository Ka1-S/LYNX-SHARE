// --- round 190fw: Image-Line's "FL Studio VSTi" in LYNX (issue #162) ------------------------------------------------
// Both FL Studio VST plugins were blacklisted by a scan child killed at the fixed 30 s limit while the FL engine was
// still booting. The parent now watches what the child does (core/PluginScanPolicy.h): a child that works or shows a
// window gets more time (up to a cap), one that sits still ends at the old limit.

#include "core/PluginScanPolicy.h"

namespace flvsti190fw
{
lynx::scanpolicy::ChildActivity act (long long cpuMs, long long readBytes = 0, long long faults = 0, int windows = 0)
{
    lynx::scanpolicy::ChildActivity a;
    a.cpuMs = cpuMs;
    a.readBytes = readBytes;
    a.faults = faults;
    a.windows = windows;
    return a;
}
} // namespace flvsti190fw

TEST (flvsti190fw_scan_child_work_is_cpu_reads_faults_or_a_window)
{
    using namespace lynx::scanpolicy;
    using flvsti190fw::act;
    // CPU: 5 % of the interval or more
    CHECK (childWorked (act (1000), act (1025), 500));
    CHECK (! childWorked (act (1000), act (1024), 500));
    CHECK (! childWorked (act (1000), act (1000), 500));
    // a disk-bound boot (FL's engine read from a cold disk) shows as reads or page faults, not CPU
    CHECK (childWorked (act (0, 0), act (0, 1 << 20), 500));
    CHECK (! childWorked (act (0, 0), act (0, (1 << 20) - 1), 500));
    CHECK (childWorked (act (0, 0, 100), act (0, 0, 356), 500));
    CHECK (! childWorked (act (0, 0, 100), act (0, 0, 355), 500));
    // a window on screen waits for the user (a licence or first-run window): counts as alive
    CHECK (childWorked (act (0), act (0, 0, 0, 1), 500));
    // a zero interval never divides by zero
    CHECK (childWorked (act (0), act (1), 0));
}

TEST (flvsti190fw_scan_child_that_sits_still_ends_at_the_old_limit)
{
    using namespace lynx::scanpolicy;
    using flvsti190fw::act;
    ChildWatch watch;
    CHECK (watch.limitMs() == baseTimeoutMs);
    // no report at all (the 32-bit bridge's child, an older LYNX): exactly the old rule
    CHECK (! watch.expired (29999));
    CHECK (watch.expired (30000));
    CHECK (std::string (watch.why (30000)) == "no sign of work");
    // reports that show nothing: same
    ChildWatch idle;
    for (int t = 0; t <= 30000; t += 500)
        idle.report (t, act (100));
    CHECK (! idle.expired (29999));
    CHECK (idle.expired (30000));
}

TEST (flvsti190fw_scan_child_that_works_gets_more_time_up_to_the_cap)
{
    using namespace lynx::scanpolicy;
    using flvsti190fw::act;
    // FL's engine still booting at 30 s: kept while it works
    ChildWatch boot;
    long long cpu = 0;
    for (int t = 0; t <= 40000; t += 500)
        boot.report (t, act (cpu += 200));
    CHECK (! boot.expired (40000));
    CHECK (std::string (boot.why (40000)) == "still busy at the limit");
    // then it stops working (a hang after a busy start): killed `stallMs` after its last work
    for (int t = 40500; t <= 70000; t += 500)
        boot.report (t, act (cpu));
    CHECK (! boot.expired (69999));
    CHECK (boot.expired (70000));
    CHECK (std::string (boot.why (70000)) == "stopped working");
    // busy for ever: ends at the cap
    ChildWatch spin;
    cpu = 0;
    for (int t = 0; t <= busyCapMs; t += 500)
        spin.report (t, act (cpu += 400));
    CHECK (! spin.expired (busyCapMs - 1));
    CHECK (spin.expired (busyCapMs));
    // a window waiting for the user: kept up to the cap
    ChildWatch dialog;
    for (int t = 0; t <= 120000; t += 500)
        dialog.report (t, act (10, 0, 0, 1));
    CHECK (! dialog.expired (120000));
    CHECK (dialog.expired (busyCapMs));
    // work only early on: no extension past the old limit plus the stall time
    ChildWatch early;
    early.report (0, act (0));
    early.report (500, act (300));
    for (int t = 1000; t <= 40000; t += 500)
        early.report (t, act (300));
    CHECK (! early.expired (30000)); // (worked at 0.5 s: dead only 30 s after that)
    CHECK (early.expired (30500));
}

TEST (flvsti190fw_scan_child_limit_still_grows_with_the_class_count)
{
    using namespace lynx::scanpolicy;
    // the Waves shell rule (round 107) is unchanged; and the cap never shortens a large shell's limit
    for (const int classes : { 0, 1, 2, 400, 100000 })
    {
        ChildWatch watch;
        watch.setClasses (classes);
        CHECK (watch.limitMs() == timeoutMs (classes));
    }
    ChildWatch shell;
    shell.setClasses (400);
    long long cpu = 0;
    for (int t = 0; t <= timeoutMs (400); t += 5000)
        shell.report (t, flvsti190fw::act (cpu += 5000));
    CHECK (! shell.expired (timeoutMs (400) - 1));
    // test limits (LYNX_TEST_SCAN_MS) are the constructor's
    ChildWatch quick (2000, 2000, 8000);
    CHECK (quick.limitMs() == 2000);
    CHECK (quick.expired (2000));
}

TEST (flvsti190fw_failed_rows_show_the_file_format)
{
    using lynx::scanpolicy::formatOfFile;
    // (a VST2 .dll was listed as "VST3" in the Plugin Manager's failed rows)
    CHECK (formatOfFile ("C:\\Program Files\\Common Files\\VST2\\FL Studio VSTi.dll") == "VST");
    CHECK (formatOfFile ("C:\\Program Files\\Common Files\\VST2\\FL Studio VSTi (Multi).DLL") == "VST");
    CHECK (formatOfFile ("/home/a/.vst/plugin.so") == "VST");
    CHECK (formatOfFile ("C:\\Program Files\\Common Files\\VST3\\Harrison\\Harrison DeNoiser.vst3") == "VST3");
    CHECK (formatOfFile ("C:\\Program Files\\Common Files\\VST3\\Sonible.vst3\\") == "VST3");
    CHECK (formatOfFile ("C:\\Program Files\\Common Files\\CLAP\\Surge XT.clap") == "CLAP");
    CHECK (formatOfFile ("urn:lynx-test:a") == "LV2");
    CHECK (formatOfFile ("http://lsp-plug.in/plugins/lv2/comp") == "LV2");
    CHECK (formatOfFile ("lv2-folders:/usr/lib/lv2") == "LV2");
    CHECK (formatOfFile ("/Library/Audio/Plug-Ins/Components/Foo.component") == "AudioUnit");
}

TEST (flvsti190fw_scan_child_reports_and_the_parent_watches)
{
    // source guards: the child writes its activity, the parent's loop uses the watch, and the files are cleaned up
    using namespace i18ntest;
    const auto host = readFile ("src/engine/PluginHost.cpp");
    CHECK (host.find ("const ActivityReporter activity (activityFile (output));") != std::string::npos);
    CHECK (host.find ("! watch.expired (elapsed())") != std::string::npos);
    CHECK (host.find ("watch.report (elapsed(), readActivity (activityFile (result)));") != std::string::npos);
    CHECK (host.find ("activityFile (result).deleteFile();") != std::string::npos);
    // the one-time retry of files blacklisted by older scans runs once more
    CHECK (host.find ("retried.loadFileAsString().getIntValue() < 3") != std::string::npos);
    CHECK (host.find ("retried.replaceWithText (\"3\\n\");") != std::string::npos);
    // and a VST2 module can be added by hand
    const auto manager = readFile ("src/ui/PluginManager.cpp");
    CHECK (manager.find ("\"*.vst3;*.clap;*.dll\"") != std::string::npos);
}
