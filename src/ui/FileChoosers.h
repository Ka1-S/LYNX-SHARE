#pragma once

// LYNX's file choosers (issue #72: "File chooser fails to open or load the selected file", Arch Linux).
//
// JUCE's native chooser on Linux runs zenity (or kdialog in a KDE session / without zenity) as a child process and reads
// the chosen path from its output. JUCE looks for them with `which` (not installed on every distribution: Arch's base
// has none) and never checks that the dialog really opened: a zenity or kdialog that cannot start (a broken GTK / Qt, a
// missing library, a display it cannot open) ends at once with nothing printed, which JUCE reports as "cancelled", so
// nothing opens and nothing loads. LYNX now:
//  - on Linux uses the native dialog only when the program JUCE would run is on PATH (searched directly, no `which`)
//    and answers `--version` (exit code 0) within 5 s; otherwise JUCE's built-in chooser (always works);
//  - when a native dialog ends without a file less than a second after it was launched (it never showed: nobody picks
//    or cancels that fast), uses the built-in chooser for the rest of the session and opens it at once in its place;
//  - gives zenity / kdialog each filter pattern in upper case too (they match case-sensitively: "*.wav" hid "LOOP.WAV");
//  - LYNX_FILE_DIALOG=builtin or =native forces one (troubleshooting).
// Windows and macOS keep their native dialogs. Every chooser of the UI goes through makeFileChooser / launchChooser.
// Message thread only.

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <map>
#include <memory>
#include <vector>

#if JUCE_LINUX || JUCE_BSD
 #include <unistd.h>
#endif

namespace lynx::ui
{
namespace filechooser
{
// what a chooser was made with (to open the built-in one in its place) and whether it is native
struct Made
{
    juce::String title, filters;
    juce::File start;
    bool native = false;
};
inline std::map<const juce::FileChooser*, Made>& made()
{
    static std::map<const juce::FileChooser*, Made> table;
    return table;
}
inline bool& brokenThisSession()
{
    static bool broken = false;
    return broken;
}
// a native dialog that ended sooner than this without a file never showed
constexpr double quickFailMs = 1000.0;

#if JUCE_LINUX || JUCE_BSD
// an executable `name` in one of PATH's folders (what `which` would find, without needing `which`)
inline bool onPath (const juce::String& name)
{
    for (const auto& dir : juce::StringArray::fromTokens (juce::SystemStats::getEnvironmentVariable ("PATH", "/usr/local/bin:/usr/bin:/bin"), ":", ""))
        if (dir.isNotEmpty())
            if (const auto f = juce::File (dir).getChildFile (name); f.existsAsFile() && ::access (f.getFullPathName().toRawUTF8(), X_OK) == 0)
                return true;
    return false;
}

// the program runs: `<name> --version` exits with 0 within 5 s
inline bool answers (const juce::String& name)
{
    juce::ChildProcess p;
    if (! p.start (juce::StringArray { name, "--version" }, juce::ChildProcess::wantStdOut))
        return false;
    if (! p.waitForProcessToFinish (5000))
    {
        p.kill();
        return false;
    }
    return p.getExitCode() == 0;
}

// The program JUCE's native chooser runs (juce_FileChooser_linux.cpp): kdialog in a KDE session or without zenity, else
// zenity. Empty when neither is on PATH.
inline juce::String nativeProgram (bool zenity, bool kdialog)
{
    const auto kde = juce::SystemStats::getEnvironmentVariable ("KDE_FULL_SESSION", {}).equalsIgnoreCase ("true");
    if (kdialog && (kde || ! zenity))
        return "kdialog";
    return zenity ? "zenity" : juce::String();
}

// zenity's and kdialog's file filters match case-sensitively (JUCE's own chooser does not): "*.wav" hid "LOOP.WAV".
// Each pattern also in upper case.
inline juce::String withUpperCase (const juce::String& filters)
{
    if (filters.isEmpty() || filters == "*" || filters == "*.*")
        return filters;
    juce::StringArray all;
    for (const auto& f : juce::StringArray::fromTokens (filters, ";,", "\""))
        if (const auto t = f.trim(); t.isNotEmpty())
            all.addIfNotAlreadyThere (t), all.addIfNotAlreadyThere (t.toUpperCase());
    return all.joinIntoString (";");
}
#endif

// Whether new choosers use the system's dialog (checked once per run on Linux)
inline bool nativeDialogs()
{
    const auto forced = juce::SystemStats::getEnvironmentVariable ("LYNX_FILE_DIALOG", {}).trim().toLowerCase();
    if (forced == "builtin" || forced == "built-in" || forced == "juce")
        return false;
    if (forced == "native")
        return true;
    if (brokenThisSession())
        return false;
#if JUCE_LINUX || JUCE_BSD
    static const bool usable = [] {
        const auto program = nativeProgram (onPath ("zenity"), onPath ("kdialog"));
        return program.isNotEmpty() && answers (program);
    }();
    return usable;
#else
    return true;
#endif
}
} // namespace filechooser

// A juce::FileChooser: the system's dialog where it works, else JUCE's built-in one (see above)
inline std::unique_ptr<juce::FileChooser> makeFileChooser (const juce::String& title, const juce::File& start = {},
                                                           const juce::String& filters = {})
{
    const auto native = filechooser::nativeDialogs();
#if JUCE_LINUX || JUCE_BSD
    auto chooser = std::make_unique<juce::FileChooser> (title, start, native ? filechooser::withUpperCase (filters) : filters, native);
#else
    auto chooser = std::make_unique<juce::FileChooser> (title, start, filters, native);
#endif
    filechooser::made()[chooser.get()] = { title, filters, start, native };
    return chooser;
}

inline std::shared_ptr<juce::FileChooser> makeSharedFileChooser (const juce::String& title, const juce::File& start = {},
                                                                 const juce::String& filters = {})
{
    return makeFileChooser (title, start, filters);
}

namespace filechooser
{
// the built-in chooser in place of a native one that never showed; `replace` puts it where the old one was kept
template <typename Replace>
void launchFallback (const Made& m, int flags, std::function<void (const juce::FileChooser&)> callback, Replace&& replace)
{
    brokenThisSession() = true;
    juce::Logger::writeToLog ("File chooser: the system dialog did not open; using the built-in one from now on");
    auto next = std::make_unique<juce::FileChooser> (m.title, m.start, m.filters, false);
    auto* raw = next.get();
    replace (std::move (next));
    raw->launchAsync (flags, std::move (callback));
}

// the callback, watching a native dialog that ends at once without a file
template <typename Replace>
std::function<void (const juce::FileChooser&)> watched (const juce::FileChooser* chooser, int flags,
                                                         std::function<void (const juce::FileChooser&)> callback, Replace replace)
{
    const auto it = made().find (chooser);
    const auto info = it != made().end() ? it->second : Made {};
    if (it != made().end())
        made().erase (it); // (read once: the chooser launches once)
    const auto forced = juce::SystemStats::getEnvironmentVariable ("LYNX_FILE_DIALOG", {}).trim().equalsIgnoreCase ("native");
    if (! info.native || forced)
        return callback;
    const auto launched = juce::Time::getMillisecondCounterHiRes();
    return [info, flags, callback, replace, launched] (const juce::FileChooser& fc) mutable {
        if (fc.getURLResults().isEmpty() && juce::Time::getMillisecondCounterHiRes() - launched < quickFailMs)
        {
            launchFallback (info, flags, callback, replace);
            return;
        }
        if (callback)
            callback (fc);
    };
}
} // namespace filechooser

// chooser->launchAsync (flags, callback), with the built-in chooser opened in its place when the system's dialog never
// showed. `holder` keeps the chooser (and, after a fallback, the built-in one that replaced it).
inline void launchChooser (std::unique_ptr<juce::FileChooser>& holder, int flags, std::function<void (const juce::FileChooser&)> callback)
{
    if (holder == nullptr)
        return;
    auto* chooser = holder.get();
    chooser->launchAsync (flags, filechooser::watched (chooser, flags, std::move (callback), [&holder] (std::unique_ptr<juce::FileChooser> next) {
                              // (called from inside the old chooser's callback: it is deleted after it returns)
                              std::shared_ptr<juce::FileChooser> old (holder.release());
                              holder = std::move (next);
                              juce::MessageManager::callAsync ([old] {});
                          }));
}

// the same for a chooser kept alive by its own callback (a std::shared_ptr captured in it)
inline void launchChooser (const std::shared_ptr<juce::FileChooser>& chooser, int flags, std::function<void (const juce::FileChooser&)> callback)
{
    if (chooser == nullptr)
        return;
    chooser->launchAsync (flags, filechooser::watched (chooser.get(), flags, std::move (callback), [] (std::unique_ptr<juce::FileChooser> next) {
                              // the built-in one replacing it lives until the end of the session (at most one per
                              // dialog that failed: the later ones are built-in from the start)
                              static std::vector<std::unique_ptr<juce::FileChooser>> kept;
                              kept.push_back (std::move (next));
                          }));
}
} // namespace lynx::ui
