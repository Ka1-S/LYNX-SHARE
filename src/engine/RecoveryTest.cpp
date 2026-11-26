// Round 190rp (0.1.32 Q4, FIX-LIST item 3): the crash-recovery round trip (engine/RecoveryTest.h).

#include "engine/RecoveryTest.h"

#include "core/ProjectSafety.h"
#include "core/TakeRecording.h"
#include "engine/AudioEngine.h"
#include "engine/ProjectFile.h"
#include "platform/CrashGuard.h"

#include <cmath>
#include <thread>

namespace lynx
{
namespace
{
// A project with something in every part a session changes: notes, steps, a tempo, a mixer level and pan, an effect,
// automation, a playlist arrangement, a name. `big`: hundreds of patterns (an autosave that takes a while to write).
Song recoverySong (bool big)
{
    auto s = Song::makeDefault();
    s.bpm = 131.5;
    s.info.title = "Recovery round trip";
    s.mixer[1].volume = 0.42f;
    s.mixer[1].pan = -0.3f;
    s.mixer[1].effects.push_back (makeEffectSlot (EffectKind::delay));
    s.patterns[0].name = "Edited before the crash";
    for (int k = 0; k < 16; ++k)
        s.patterns[0].notes.push_back ({ (std::int64_t) k * ppq / 4, ppq / 8, k % (int) s.channels.size(), 48 + (k * 5) % 24, 0.5f + 0.03f * (float) k });
    s.patterns[0].sortNotes();
    Automation a;
    a.name = "Insert 1 volume";
    a.target = { AutomationTargetKind::mixerVolume, 1, 0, 0 };
    a.points = { { 0, 0.1f, 0.0f }, { 4 * ppq, 0.9f, 0.3f } };
    s.automations.push_back (a);
    for (int bar = 0; bar < 4; ++bar)
        s.playlist.push_back ({ 0, 0, bar * 4 * ppq, 4 * ppq, bar % 3 });
    if (big)
        for (int p = 0; p < 400; ++p)
        {
            auto& pattern = s.patterns.emplace_back();
            pattern.name = "Filler " + std::to_string (p);
            for (int k = 0; k < 64; ++k)
                pattern.notes.push_back ({ (std::int64_t) k * ppq / 4, ppq / 8, k % 4, 40 + (k * 7 + p) % 40, 0.7f });
            pattern.sortNotes();
        }
    return s;
}

juce::String jsonOf (const juce::File& file, juce::String& error)
{
    auto song = ProjectFile::load (file, error);
    return song ? ProjectFile::toJson (*song, false) : juce::String();
}

std::vector<safety::BackupFile> backupsIn (const juce::File& folder)
{
    std::vector<safety::BackupFile> files;
    for (const auto& f : folder.findChildFiles (juce::File::findFiles, false, juce::String ("*") + ProjectFile::extension))
        files.push_back ({ f.getFileNameWithoutExtension().toStdString(), f.getLastModificationTime().toMilliseconds() });
    return files;
}

// (a save writes "<name>_temp<hex>.lynx" through juce::TemporaryFile, whose text goes through File::replaceWithText's
// own hidden temporary ".<name>_temp<hex>_temp<hex>.lynx" first: either may be left)
juce::File temporaryIn (const juce::File& folder, const juce::String& stem)
{
    for (const auto& f : folder.findChildFiles (juce::File::findFiles, false, juce::String ("*") + ProjectFile::extension))
        if (const auto name = f.getFileNameWithoutExtension(); name.trimCharactersAtStart (".").startsWith (stem)
                                                               && safety::isTemporarySaveName (name.toStdString()))
            return f;
    return {};
}

// the child, started with `kind`; killed (SIGKILL / TerminateProcess: nothing of LYNX runs after it, as after a power
// cut) once `ready` says so or `ms` passed. False when it could not start.
bool runAndKill (const juce::String& kind, const juce::File& folder, const std::function<bool()>& ready, int ms)
{
    const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    juce::ChildProcess child;
    if (! child.start (juce::StringArray { exe.getFullPathName(), "--recoverychild", kind, folder.getFullPathName() }, 0))
        return false;
    const auto start = juce::Time::getMillisecondCounter();
    while (child.isRunning() && juce::Time::getMillisecondCounter() - start < (juce::uint32) ms)
    {
        if (ready && ready())
            break;
        std::this_thread::sleep_for (std::chrono::microseconds (200));
    }
    child.kill();
    child.waitForProcessToFinish (10000);
    return true;
}
} // namespace

bool runRecoveryChild (const juce::StringArray& args)
{
    const auto i = args.indexOf ("--recoverychild");
    if (i < 0)
        return false;
    const auto kind = args[i + 1];
    const juce::File folder (args[i + 2].unquoted());
    if (kind == "autosave")
    {
        // what MainComponent::autosave writes, one backup after another (each a new name, as the minutes go by)
        const auto song = recoverySong (true);
        for (int n = 0;; ++n)
        {
            const auto name = safety::backupName ("Recovery", safety::BackupKind::autosaved, 2026, 11, 26, n / 60 % 24, n % 60);
            juce::String error;
            ProjectFile::save (song, folder.getChildFile (juce::String (name) + ProjectFile::extension), error);
        }
    }
    if (kind == "save")
    {
        // File › Save over the same project, two versions in turn
        auto a = recoverySong (true), b = a;
        b.bpm = 99.0;
        b.patterns[0].name = "Version B";
        for (int n = 0;; ++n)
        {
            juce::String error;
            ProjectFile::save (n % 2 == 0 ? a : b, folder.getChildFile ("Project.lynx"), error);
        }
    }
    if (kind == "take")
    {
        // a stereo take as the engine's recording thread writes it (blocks of 512 at 48 kHz, in real time), with its
        // journal; progress.txt says how much was handed to the writer (every 50 ms)
        const auto wav = folder.getChildFile ("Audio_Take1.wav");
        takes::WavTakeWriter writer;
        if (! writer.open (wav.getFullPathName().toStdString(), 48000.0, 2, takes::SampleFormat::pcm24))
            crashguard::exitNow (5);
        takes::Journal j;
        j.wav = wav.getFullPathName().toStdString();
        j.sampleRate = 48000.0;
        j.ticksPerSample = 0.02;
        folder.getChildFile ("Audio_Take1.wav.lynxrec").replaceWithText (juce::String::fromUTF8 (takes::journalText (j).c_str()));
        std::vector<float> l (512), r (512);
        const float* data[] = { l.data(), r.data() };
        std::int64_t written = 0;
        const auto start = juce::Time::getMillisecondCounterHiRes();
        for (int block = 0;; ++block)
        {
            for (int k = 0; k < 512; ++k)
            {
                const auto t = (double) (written + k) / 48000.0;
                l[(std::size_t) k] = r[(std::size_t) k] = (float) (0.25 * std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * t));
            }
            writer.write (data, 512);
            written += 512;
            if (block % 5 == 0)
                folder.getChildFile ("progress.txt").replaceWithText (juce::String (written));
            const auto due = start + (double) written / 48.0;
            while (juce::Time::getMillisecondCounterHiRes() < due)
                std::this_thread::sleep_for (std::chrono::milliseconds (1));
        }
    }
    crashguard::exitNow (6);
}

juce::String recoveryRoundTripSelfTest (juce::String& log)
{
    const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("lynx-recovery190rp-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()));
    struct Cleanup
    {
        juce::File f;
        ~Cleanup()
        {
            if (juce::SystemStats::getEnvironmentVariable ("LYNX_RECOVERYTEST_KEEP", {}) != "1") // (diagnosis: keep the children's files)
                f.deleteRecursively();
        }
    } cleanup { root };
    juce::StringArray problems;
    auto say = [&log] (const juce::String& line) { log << line << "\n"; };
    const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);

   #if (JUCE_WINDOWS && (defined (_M_X64) || defined (__x86_64__))) \
       || ((JUCE_LINUX || JUCE_MAC) && (defined (__x86_64__) || defined (__aarch64__)))
    // 1. a crash mid-session (LYNX --faulttest recovery: the project in an engine, then an access violation): the crash
    //    handler's "recovered" project is what the next start offers, and it opens as the song that was there
    {
        const auto folder = root.getChildFile ("crash");
        folder.createDirectory();
        juce::String error;
        const auto before = folder.getChildFile ("before.lynx");
        if (! ProjectFile::save (recoverySong (false), before, error))
            problems.add ("crash: " + error);
        // the next start's question: files written since the crashed session started (the folder's files are older)
        const auto sessionStart = juce::Time::getCurrentTime().toMilliseconds() + 1000;
        juce::Thread::sleep (1100);
        juce::ChildProcess child;
        const auto started = child.start (juce::StringArray { exe.getFullPathName(), "--faulttest", "recovery", folder.getFullPathName() }, 0);
        if (! started || ! child.waitForProcessToFinish (60000))
        {
            child.kill();
            problems.add (juce::String ("crash: the child ") + (started ? "did not end" : "did not start"));
        }
        else
        {
            juce::String crashLine;
            for (const auto& f : folder.findChildFiles (juce::File::findFiles, false, "crash *.txt"))
                crashLine = f.loadFileAsString().fromFirstOccurrenceOf ("--- frames", false, false).trim().upToFirstOccurrenceOf ("\n", false, false);
            say ("crash mid-session: the crash file says \"" + crashLine + "\"; files: "
                 + juce::String (folder.findChildFiles (juce::File::findFiles, false, "*").size()));
            const auto name = safety::crashRecoveryCandidate (backupsIn (folder), sessionStart);
            const auto offered = folder.getChildFile (juce::String::fromUTF8 (name.c_str()) + ProjectFile::extension);
            juce::String e1, e2;
            const auto a = jsonOf (before, e1), b = name.empty() ? juce::String() : jsonOf (offered, e2);
            say ("crash mid-session: exit code 0x" + juce::String::toHexString ((juce::int64) child.getExitCode()) + ", offered \"" + juce::String (name)
                 + "\", " + (b.isEmpty() ? "does not open: " + e2 : juce::String (a == b ? "the same song" : "a different song")));
            if (! juce::String (name).startsWith ("recovered "))
                problems.add ("crash: offered \"" + juce::String (name) + "\" instead of the recovered project");
            else if (b.isEmpty() || a != b)
                problems.add ("crash: the recovered project " + juce::String (b.isEmpty() ? "does not open (" + e2 + ")" : "differs from the song"));
        }
    }

    // 2. killed while an autosave writes its file: the half-written temporary file must not be offered
    {
        const auto folder = root.getChildFile ("autosave");
        folder.createDirectory();
        const auto sessionStart = juce::Time::getCurrentTime().toMilliseconds() - 1000;
        bool caught = false;
        for (int attempt = 0; attempt < 8 && ! caught; ++attempt)
        {
            // (killed once a backup is complete and the next one's temporary file is there)
            runAndKill ("autosave", folder, [&folder] { return folder.findChildFiles (juce::File::findFiles, false, "Recovery (autosaved*).lynx").size() > 1
                                                               && temporaryIn (folder, "Recovery").exists(); }, 60000);
            caught = temporaryIn (folder, "Recovery").exists();
        }
        const auto temp = temporaryIn (folder, "Recovery");
        const auto name = safety::crashRecoveryCandidate (backupsIn (folder), sessionStart);
        juce::String error;
        const auto opens = ! name.empty() && ProjectFile::load (folder.getChildFile (juce::String::fromUTF8 (name.c_str()) + ProjectFile::extension), error).has_value();
        say ("killed during an autosave: " + juce::String (caught ? "a half-written " + temp.getFileName() + " (" + juce::String (temp.getSize()) + " bytes) was left"
                                                                  : "no temporary file was caught")
             + ", offered \"" + juce::String (name) + "\" (" + (opens ? "opens" : "does not open: " + error) + ")");
        if (! caught)
            problems.add ("autosave: the kill never caught a file being written");
        if (safety::isTemporarySaveName (name) || ! opens)
            problems.add ("autosave: offered \"" + juce::String (name) + "\"" + (opens ? "" : " (" + error + ")"));
    }

    // 3. killed while File › Save writes over the project: the project is the old or the new version, whole
    {
        const auto folder = root.getChildFile ("save");
        folder.createDirectory();
        int whole = 0, tries = 0, caughtWriting = 0;
        auto a = recoverySong (true), b = a;
        b.bpm = 99.0;
        b.patterns[0].name = "Version B";
        // (each version as it reads back from a whole file, so both sides went through the same load)
        juce::String refError;
        ProjectFile::save (a, root.getChildFile ("version-a.lynx"), refError);
        ProjectFile::save (b, root.getChildFile ("version-b.lynx"), refError);
        const auto ja = jsonOf (root.getChildFile ("version-a.lynx"), refError), jb = jsonOf (root.getChildFile ("version-b.lynx"), refError);
        if (ja.isEmpty() || jb.isEmpty() || ja == jb)
            problems.add ("save: the two versions could not be written apart (" + refError + ")");
        for (; tries < 4; ++tries)
        {
            runAndKill ("save", folder, [&folder] { return folder.getChildFile ("Project.lynx").existsAsFile() && temporaryIn (folder, "Project").exists(); }, 60000);
            if (temporaryIn (folder, "Project").exists())
                ++caughtWriting;
            juce::String error;
            const auto j = jsonOf (folder.getChildFile ("Project.lynx"), error);
            if (j == ja || j == jb)
                ++whole;
            else
                problems.add ("save: after kill " + juce::String (tries + 1) + " the project " + (j.isEmpty() ? "does not open: " + error : juce::String ("is neither version")));
        }
        say ("killed during File > Save: " + juce::String (whole) + " / " + juce::String (tries) + " times the project was a whole version ("
             + juce::String (caughtWriting) + " kills while a save was writing)");
        if (caughtWriting == 0)
            problems.add ("save: no kill came while a save was writing");
    }

    // 4. killed mid-take: the take repaired from its journal, missing at most the writer's last flush period
    {
        const auto folder = root.getChildFile ("take");
        folder.createDirectory();
        runAndKill ("take", folder, {}, 2600);
        const auto written = folder.getChildFile ("progress.txt").loadFileAsString().trim().getLargeIntValue();
        AudioEngine engine (false);
        const auto found = engine.recoverInterruptedTakes ({ folder });
        std::int64_t frames = 0;
        const auto repaired = ! found.empty() && takes::repairWav (found.front().wav, &frames);
        const auto lostSeconds = (double) (written - frames) / 48000.0;
        say ("killed mid-take: " + juce::String ((double) written / 48000.0, 2) + " s handed to the writer, " + juce::String ((double) frames / 48000.0, 2)
             + " s recovered from the journal (" + juce::String (juce::jmax (0.0, lostSeconds), 2) + " s lost)");
        if (! repaired)
            problems.add ("take: not recovered (" + juce::String ((int) found.size()) + " journals)");
        else if (written <= 0 || lostSeconds > 1.2)
            problems.add ("take: " + juce::String (lostSeconds, 2) + " s lost");
    }
   #else
    juce::ignoreUnused (exe, say);
    return "SKIPPED (Windows x64, Linux / macOS x86-64 / ARM64 only)";
   #endif
    return problems.isEmpty() ? juce::String ("OK") : "ERROR " + problems.joinIntoString ("; ");
}
} // namespace lynx
