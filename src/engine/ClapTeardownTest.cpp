#include "engine/ClapTeardownTest.h"

#include <atomic>
#include <cstdlib>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

#include "engine/AudioEngine.h"
#include "engine/ClapFormat.h"
#include "engine/PluginHost.h"

namespace lynx
{
namespace
{
struct TeardownTest : std::enable_shared_from_this<TeardownTest>
{
    // what the message thread does while the worker drops the plugin, and how the queued teardown then gets to run
    enum class Kind
    {
        free,            // it runs its loop: the teardown is done while the worker waits for it
        blockedThenLoop, // it joins the worker; afterwards it goes back to its loop, which runs the teardown
        blockedThenExit  // it joins the worker; then what the application's shutdown calls runs the teardown
    };
    struct Round
    {
        Kind kind;
        int waitMs; // how long the deleting thread waits for the message thread
    };

    juce::String pluginPath, reportPath, report;
    juce::File counters;
    std::function<void (int)> done;
    std::vector<Round> rounds { { Kind::free, 10000 }, { Kind::blockedThenLoop, 300 }, { Kind::blockedThenExit, 300 }, { Kind::blockedThenLoop, 10000 } };
    std::size_t next = 0;
    bool ok = true;

    // the round in progress
    std::thread worker;
    std::atomic<bool> dropped { false };
    std::atomic<int> pendingAfterDrop { -1 };
    std::atomic<double> dropMs { 0.0 };
    int linesBefore = 0, deferredBefore = 0;

    void fail (const juce::String& why)
    {
        report << "FAILED: " << why << "\n";
        ok = false;
    }

    juce::StringArray lines() const
    {
        juce::StringArray out;
        for (const auto& line : juce::StringArray::fromLines (counters.loadFileAsString()))
            if (line.contains ("="))
                out.add (line);
        return out;
    }

    // A CLAP effect that is active and processing, held by this reference only: made and played by an engine (on the
    // message thread), which is then closed; then prepared and given one more block, as a rack does.
    std::shared_ptr<PluginEffect> runningPlugin()
    {
        auto engine = std::make_unique<AudioEngine> (false);
        auto& host = engine->getPluginHost();
        host.addPluginFile (juce::File (pluginPath));
        std::optional<juce::PluginDescription> clap;
        for (const auto& type : host.getKnownPlugins().getTypes())
            if (type.pluginFormatName == ClapPluginFormat::formatName
                && juce::File (type.fileOrIdentifier).getFileName() == juce::File (pluginPath).getFileName())
                clap = type;
        if (! clap)
            return nullptr;
        auto song = Song::makeDefault();
        EffectSlot slot;
        slot.kind = EffectKind::plugin;
        slot.plugin.id = clap->createIdentifierString().toStdString();
        slot.plugin.name = clap->name.toStdString();
        slot.smartDisable = false;
        song.mixer[0].effects = { slot };
        engine->setSong (song);
        engine->renderOffline (0.1, 48000.0);
        auto effect = std::dynamic_pointer_cast<PluginEffect> (engine->getEffect (0, 0));
        engine.reset();
        if (effect == nullptr)
            return nullptr;
        auto& instance = effect->getInstance();
        instance.prepareToPlay (48000.0, 512);
        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        buffer.clear();
        instance.processBlock (buffer, midi);
        return effect;
    }

    void start()
    {
        counters = juce::File (reportPath).getSiblingFile (juce::File (reportPath).getFileNameWithoutExtension() + "-plugin.txt");
        counters.deleteFile();
       #if JUCE_WINDOWS
        _putenv_s ("LYNX_TEST_RESTART_REPORT", counters.getFullPathName().toRawUTF8());
       #else
        setenv ("LYNX_TEST_RESTART_REPORT", counters.getFullPathName().toRawUTF8(), 1);
       #endif
        runRound();
    }

    void runRound()
    {
        if (next >= rounds.size() || ! ok)
            return finish();
        const auto round = rounds[next];
        auto effect = runningPlugin();
        if (effect == nullptr)
        {
            fail ("no CLAP effect was loaded from the file");
            return finish();
        }
        // (the engine that made it is closed: every instance but this one has written its line)
        linesBefore = lines().size();
        deferredBefore = ClapPluginFormat::deferredTeardowns();
        ClapPluginFormat::setTeardownWaitMs (round.waitMs);
        dropped = false;
        pendingAfterDrop = -1;
        worker = std::thread ([this, effect = std::move (effect)]() mutable {
            const auto start = juce::Time::getMillisecondCounterHiRes();
            effect.reset(); // the last reference: the plugin's instance is deleted here, on this thread
            dropMs = juce::Time::getMillisecondCounterHiRes() - start;
            pendingAfterDrop = ClapPluginFormat::pendingTeardowns();
            dropped = true;
        });

        if (round.kind == Kind::free)
            return waitForWorker();

        // blocked on the thread that deletes the plugin: it can't have the message thread, however long it waits
        worker.join();
        const auto pendingNow = ClapPluginFormat::pendingTeardowns();
        const auto linesNow = lines().size();
        report << "round=" << (int) next + 1 << " kind=" << (round.kind == Kind::blockedThenLoop ? "blocked_then_loop" : "blocked_then_exit")
               << " wait_ms=" << round.waitMs << " drop_ms=" << juce::String (dropMs.load(), 0) << " pending_after_drop=" << pendingAfterDrop.load()
               << " pending_after_join=" << pendingNow << " destroyed_before_message_thread=" << linesNow - linesBefore << "\n";
        if (pendingAfterDrop != 1 || pendingNow != 1)
            fail ("the teardown was not left to the message thread");
        if (linesNow != linesBefore)
            fail ("the plugin was destroyed while the message thread was blocked");
        if (ClapPluginFormat::deferredTeardowns() != deferredBefore + 1)
            fail ("the deleting thread did not go on without the teardown");
        if (round.kind == Kind::blockedThenExit)
        {
            ClapPluginFormat::finishPendingTeardowns(); // (what the application's shutdown calls)
            return endRound();
        }
        juce::Timer::callAfterDelay (200, [self = shared_from_this()] { self->endRound(); }); // the loop runs it
    }

    void waitForWorker()
    {
        if (! dropped)
        {
            juce::Timer::callAfterDelay (20, [self = shared_from_this()] { self->waitForWorker(); });
            return;
        }
        worker.join();
        report << "round=" << (int) next + 1 << " kind=free wait_ms=" << rounds[next].waitMs << " drop_ms=" << juce::String (dropMs.load(), 0)
               << " pending_after_drop=" << pendingAfterDrop.load() << "\n";
        if (pendingAfterDrop != 0 || ClapPluginFormat::deferredTeardowns() != deferredBefore)
            fail ("a free message thread did not run the teardown while the deleting thread waited");
        endRound();
    }

    // the teardown has run: one more instance line, written by a plugin that was stopped, deactivated and destroyed
    // in order and on the right threads (it would have aborted the process otherwise)
    void endRound()
    {
        const auto now = lines();
        if (ClapPluginFormat::pendingTeardowns() != 0)
            fail ("a teardown is still queued");
        if (now.size() != linesBefore + 1)
            fail ("the plugin was not destroyed (instance lines: " + juce::String (now.size()) + ", expected " + juce::String (linesBefore + 1) + ")");
        else
        {
            const auto line = now[now.size() - 1];
            report << "instance: " << line << "\n";
            juce::StringPairArray values;
            for (const auto& token : juce::StringArray::fromTokens (line, " ", ""))
                if (token.contains ("="))
                    values.set (token.upToFirstOccurrenceOf ("=", false, false), token.fromFirstOccurrenceOf ("=", false, false));
            if (values["wrong_thread"].getIntValue() != 0 || values["misordered"].getIntValue() != 0)
                fail ("a call on the wrong thread or out of order");
            if (values["starts"].getIntValue() < 1 || values["starts"].getIntValue() != values["stops"].getIntValue())
                fail ("start_processing without its stop_processing");
            if (values["activations"].getIntValue() < 1 || values["activations"].getIntValue() != values["deactivations"].getIntValue())
                fail ("activate without its deactivate");
        }
        ++next;
        juce::Timer::callAfterDelay (10, [self = shared_from_this()] { self->runRound(); });
    }

    void finish()
    {
        ClapPluginFormat::setTeardownWaitMs (10000);
        report << "instances=" << lines().size() << "\nplugin_crashes=" << pluginCrashCount() << "\n";
        if (pluginCrashCount() != 0)
            ok = false;
        report << "result=" << (ok ? "OK" : "FAILED") << "\n";
        juce::File (reportPath).replaceWithText (report);
        if (done)
            done (ok ? 0 : 1);
    }
};
} // namespace

void runClapTeardownTest (const juce::String& pluginPath, const juce::String& reportPath, std::function<void (int)> done)
{
    auto test = std::make_shared<TeardownTest>();
    test->pluginPath = pluginPath;
    test->reportPath = reportPath;
    test->done = std::move (done);
    juce::Timer::callAfterDelay (10, [test] { test->start(); });
}
} // namespace lynx
