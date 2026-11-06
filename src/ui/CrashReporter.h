#pragma once

// Crash / freeze reports (round 159), the app side of core/CrashReport.h:
// - after LYNX closed unexpectedly, the next start asks once (Send / Show what will be sent / Don't send, "Always send
//   without asking", "Report on GitHub instead", "Open the folder with the report") — or sends unasked / never,
//   as Settings › General › "Crash reports" says (default: ask);
// - Help › "Report a problem…" sends a manual report (a description, an optional e-mail, the newest crash / freeze
//   files and this session's steps);
// - reports go by HTTPS POST (gzip JSON) to lynx-share.com/api/crash.php on a background thread; a report that could
//   not be sent (offline) is tried once more at the next start, then dropped. Sent ids are listed in
//   %APPDATA%\LYNX\crash-reports\sent.log (to ask for a report's deletion).

#include <functional>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "core/CrashReport.h"

namespace lynx::crashreporter
{
crashreport::Consent consent();
void setConsent (crashreport::Consent);

// What an earlier session's breadcrumb file (sessions\session-<pid>.txt, round 111) holds.
struct Session
{
    juce::String version;
    juce::StringArray steps;   // "HH:MM:SS step", oldest first
    juce::StringArray plugins; // "name|vendor|format|version[|separate process]"
    juce::StringPairArray info; // info.<key> lines (audio)
    juce::File crashFile, hangFile;
    // "crash" (crash file, or a hang file sampled inside Windows' fatal-error report), "hang" (ended while frozen),
    // else "unclean"
    juce::String kind() const;
};
Session parseSession (const juce::String& sessionFileText);

// The report of `session` (`kind` "manual" for Help › Report a problem), with this computer's facts.
crashreport::ReportInput makeInput (const Session& session, const juce::String& kind, juce::AudioDeviceManager* devices);
crashreport::Anonymiser makeAnonymiser (bool keepFileNames);
juce::String buildJson (const crashreport::ReportInput& input, bool keepFileNames);
// The GitHub form prefilled from the same facts (anonymised, never file names)
juce::String githubUrl (const crashreport::ReportInput& input);

// Queues the JSON (gzip) and sends it on the background thread; `done` (message thread) gets the result.
void send (const juce::String& json, const juce::String& id, std::function<void (bool sent, const juce::String& message)> done = {});
// The reports an earlier run could not send: one more try each (start-up)
void retryPending();
// Shutdown: give an upload in flight a moment, never block for long
void shutdown();
// Synchronous upload, for `LYNX.exe --send-test-report` (returns "" on success, else the error)
juce::String sendNow (const juce::String& json);
juce::String endpoint(); // https://lynx-share.com/api/crash.php (LYNX_CRASH_ENDPOINT overrides, for tests)

// Keeps the session file's "info.audio" line up to date with the open audio device.
void watchAudioDevice (juce::AudioDeviceManager& devices);

// The start-up question after an unclean end. `notice` is the "closed unexpectedly" text (with what LYNX did about
// it); when `separatePlugin` is not empty the dialog also offers to run that plugin in its own process
// (`onSeparate` applies it).
void showStartupDialog (const Session& session, const juce::String& notice, const juce::String& separatePlugin,
                        std::function<void()> onSeparate, juce::AudioDeviceManager* devices);
// Help › Report a problem…
void showManualReport (juce::AudioDeviceManager* devices);
// For --snapshot: the two dialogs' contents
std::unique_ptr<juce::Component> makeDialogForSnapshot (bool manual);
} // namespace lynx::crashreporter
