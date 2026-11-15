// Round 190fp: the two release problems of 0.1.27.
// (1) The Linux plugin download was missing from the release: every host on Ubuntu 22.04 (glibc 2.35) failed to load
//     LYNX Plugins.clap and the VST3s ("cannot allocate memory in static TLS block"). The crash guard's thread_local
//     frame pointer (platform/CrashGuard.cpp, round 190dt) was initial-exec; in a module a host loads with dlopen that
//     one variable makes the module's whole TLS block static (DF_STATIC_TLS), and glibc keeps only a small reserve for
//     those. In the exported plugins' runtime the frame is now a pthread key (as on macOS), and
//     scripts/check-linux-compat.sh fails a library that still asks for static TLS.
// (2) Finishing the setup wizard started a full plugin scan even with "Look for new plugins in the background at every
//     start" unticked (QA 190fd O1: 20+ licence boxes on a PC with expired trials). core/AutoScanPolicy.h decides.

#include "core/AutoScanPolicy.h"

TEST (relfix190fp_wizard_finish_scans_only_when_asked)
{
    using namespace lynx::autoscan;
    // ticked (the default): Finish starts the scan, unless "Scan now" already did
    CHECK (onWizardFinish (true, false));
    CHECK (! onWizardFinish (true, true));
    // unticked: no scan on Finish, whether or not one already ran
    CHECK (! onWizardFinish (false, false));
    CHECK (! onWizardFinish (false, true));
    // start-up: only a persistent window, after the wizard, with the box on
    CHECK (atStartup (true, true, true));
    CHECK (! atStartup (true, true, false));
    CHECK (! atStartup (true, false, true));
    CHECK (! atStartup (false, true, true));
    static_assert (! onWizardFinish (false, false) && atStartup (true, true, true));
}

TEST (relfix190fp_wizard_and_start_use_the_policy)
{
    using namespace i18ntest;
    const auto wizard = readFile ("src/ui/SetupWizard.cpp");
    const auto finish = wizard.find ("void finishSetup()");
    CHECK (finish != std::string::npos);
    const auto end = wizard.find ("\n    }\n", finish);
    CHECK (end != std::string::npos);
    const auto body = wizard.substr (finish, end - finish);
    CHECK (body.find ("autoscan::onWizardFinish (scanAtStart.getToggleState(), scanStarted)") != std::string::npos);
    // the old unconditional start is gone
    CHECK (body.find ("if (! scanStarted)") == std::string::npos);
    // "Scan now" still scans at once
    CHECK (wizard.find ("scanNow.onClick") != std::string::npos);

    const auto main = readFile ("src/ui/MainComponent.cpp");
    CHECK (main.find ("autoscan::atStartup (persistent, setupWizardDone(), backgroundScanAtStart())") != std::string::npos);
}

TEST (relfix190fp_crash_guard_has_no_static_tls_in_plugins)
{
    using namespace i18ntest;
    const auto guard = readFile ("src/platform/CrashGuard.cpp");
    const auto tls = guard.find ("tls_model (\"initial-exec\")");
    CHECK (tls != std::string::npos);
    // the initial-exec variable sits in the #else of a branch that the exported plugins' runtime takes
    const auto branch = guard.rfind ("#if defined(__APPLE__) || defined(LYNX_GUARD_FRAME_KEY)", tls);
    CHECK (branch != std::string::npos);
    const auto elseAt = guard.find ("#else", branch);
    CHECK (elseAt != std::string::npos && elseAt < tls && guard.find ("#endif", branch) > tls);
    // ...set by the define only that runtime has (cmake/LynxPlugins.cmake)
    const auto define = guard.find ("#if defined(LYNX_EXPORT_VERSION)");
    CHECK (define != std::string::npos && define < branch);
    CHECK (guard.find ("#define LYNX_GUARD_FRAME_KEY 1", define) < branch);
    // the key is created on install for the same builds
    CHECK (guard.find ("#if defined(__APPLE__) || defined(LYNX_GUARD_FRAME_KEY)\n    pthread_key_create (&frameKey, nullptr);")
           != std::string::npos);
    const auto plugins = readFile ("cmake/LynxPlugins.cmake");
    const auto defines = plugins.find ("set(LYNX_PLUGIN_DEFINES");
    CHECK (defines != std::string::npos && plugins.find ("LYNX_EXPORT_VERSION=", defines) != std::string::npos);
    CHECK (plugins.find ("target_compile_definitions(lynx_plugin_runtime PUBLIC ${LYNX_PLUGIN_DEFINES})") != std::string::npos);

    // the release check fails a Linux library that asks for static TLS
    const auto compat = readFile ("scripts/check-linux-compat.sh");
    const auto check = compat.find ("STATIC_TLS");
    CHECK (check != std::string::npos);
    CHECK (compat.find ("fail=1", check) != std::string::npos);
}
