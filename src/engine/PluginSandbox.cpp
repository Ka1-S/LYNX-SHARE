#include "engine/PluginSandbox.h"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <map>
#include <mutex>
#include <thread>

#include <juce_gui_extra/juce_gui_extra.h>

#include "core/Denormals.h"
#include "engine/PluginHost.h"
#include "ui/I18n.h"
#include "ui/Theme.h"
#include "ui/WindowOwner.h"

#include "core/PeArch.h"
#include "engine/SandboxShared.h"
#include "engine/SandboxWorker.h"

static_assert (lynx::PluginHost::maxBlockSize == 4096, "SandboxShared.h: SharedBlock::maxFrames");

namespace lynx
{
using namespace sandboxipc;

bool pluginSandboxEnabled()
{
    return PluginHost::settingsFolder().getChildFile ("sandbox.txt").loadFileAsString().trim() == "1";
}

void setPluginSandboxEnabled (bool enabled)
{
    PluginHost::settingsFolder().getChildFile ("sandbox.txt").replaceWithText (enabled ? "1" : "0");
}

namespace
{
juce::File separateListFile() { return PluginHost::settingsFolder().getChildFile ("separate-process.txt"); }
juce::StringArray separateList()
{
    auto lines = juce::StringArray::fromLines (separateListFile().loadFileAsString());
    lines.trim();
    lines.removeEmptyStrings();
    return lines;
}
} // namespace

bool pluginAlwaysSeparate (const juce::String& name)
{
    return name.isNotEmpty() && separateList().contains (name.trim(), true);
}

juce::StringArray pluginsAlwaysSeparate()
{
    return separateList();
}

void setPluginAlwaysSeparate (const juce::String& name, bool on)
{
    if (name.trim().isEmpty())
        return;
    auto list = separateList();
    list.removeString (name.trim(), true);
    if (on)
        list.add (name.trim());
    separateListFile().replaceWithText (list.joinIntoString ("\n") + (list.isEmpty() ? "" : "\n"));
}

namespace
{
std::atomic<bool> showAlerts { true };

// separate-process plugins that died this session (round 176a): fixed slots, written from any thread without allocating
constexpr int deathSlots = 16;
struct DeathSlot
{
    char name[128] {};
    bool crashed = false, bits32 = false;
};
std::array<DeathSlot, deathSlots> deathSlotsArray;
std::atomic<int> deathCount { 0 };

void recordDeath (const char* name, bool crashed, bool bits32) noexcept
{
    const auto n = deathCount.load (std::memory_order_relaxed);
    auto& slot = deathSlotsArray[(std::size_t) (n % deathSlots)];
    std::memcpy (slot.name, name, sizeof (slot.name));
    slot.name[sizeof (slot.name) - 1] = 0;
    slot.crashed = crashed;
    slot.bits32 = bits32;
    deathCount.store (n + 1, std::memory_order_release);
}
} // namespace

int pluginSandboxDeathCount() noexcept
{
    return deathCount.load (std::memory_order_acquire);
}

std::vector<SandboxDeath> pluginSandboxDeaths()
{
    std::vector<SandboxDeath> out;
    const auto n = deathCount.load (std::memory_order_acquire);
    for (int i = juce::jmax (0, n - deathSlots); i < n; ++i)
    {
        const auto& slot = deathSlotsArray[(std::size_t) (i % deathSlots)];
        out.push_back ({ juce::String::fromUTF8 (slot.name), slot.crashed, slot.bits32 });
    }
    return out;
}

void setPluginSandboxAlerts (bool show)
{
    showAlerts = show;
}

// --- the child side's hooks in LYNX.exe (engine/SandboxWorker.h) ---------------------------------------------------
namespace sandboxhost
{
std::unique_ptr<juce::AudioPluginInstance> open (const juce::PluginDescription& description, double sampleRate, int blockSize, juce::String& error)
{
    PluginHost host (false);
    if (description.pluginFormatName == "LV2")
        host.findLv2Plugins(); // (an LV2 URI from a folder outside JUCE's two default ones; round 161)
    return createRightInstance (host.getFormats(), description, sampleRate, blockSize, error);
}
void prepare (juce::AudioPluginInstance& plugin, double sampleRate, int blockSize) { preparePlugin (plugin, sampleRate, blockSize); }
std::string captureState (juce::AudioPluginInstance& plugin) { return capturePluginState (plugin); }
juce::Colour windowColour() { return theme::colour::bg1; }
juce::Image windowIcon() { return logoImage(); }
unsigned x87ControlWord() { return 0; } // (64-bit: no x87 in plugins' maths)
} // namespace sandboxhost

// --- 32-bit plugins (round 176a) -----------------------------------------------------------------------------------
bool bridge32Supported()
{
   #if JUCE_WINDOWS && (defined(__x86_64__) || defined(_M_X64))
    return true;
   #else
    return false;
   #endif
}

juce::File bridge32Executable()
{
    // LYNX_BRIDGE32 (tests): another bridge executable
    if (const auto custom = juce::SystemStats::getEnvironmentVariable ("LYNX_BRIDGE32", {}); custom.isNotEmpty())
        return juce::File (custom);
    return juce::File::getSpecialLocation (juce::File::currentExecutableFile).getSiblingFile ("LYNX-bridge32.exe");
}

namespace
{
pearch::Arch fileArch (const juce::File& file)
{
    juce::FileInputStream in (file);
    if (! in.openedOk())
        return pearch::Arch::unknown;
    std::uint8_t head[pearch::headerBytes] {};
    const auto got = in.read (head, (int) sizeof (head));
    return pearch::fromHeader (head, (std::size_t) juce::jmax (0, got));
}

// a module's architecture: a DLL / single-file .vst3 by its header; a VST3 bundle by the module it has (a bundle with a
// 64-bit module is 64-bit even when it also carries a 32-bit one: LYNX loads that one itself)
pearch::Arch moduleArch (const juce::String& path)
{
    if (! juce::File::isAbsolutePath (path))
        return pearch::Arch::unknown;
    const juce::File f (path);
    if (f.isDirectory())
    {
        const auto contents = f.getChildFile ("Contents");
        for (auto arch : { pearch::Arch::x64, pearch::Arch::arm64, pearch::Arch::x86 })
            if (const auto module = contents.getChildFile (pearch::vst3Folder (arch)).getChildFile (f.getFileName()); module.existsAsFile())
                return fileArch (module);
        return pearch::Arch::unknown;
    }
    return f.existsAsFile() ? fileArch (f) : pearch::Arch::unknown;
}
} // namespace

bool isModule32Bit (const juce::String& fileOrIdentifier)
{
    if (! bridge32Supported())
        return false;
    // cached by path and date: the plugin menus and the scan ask often, and the answer only changes with the file
    static std::mutex lock;
    static std::map<juce::String, std::pair<juce::int64, bool>> cache;
    const juce::File f (juce::File::isAbsolutePath (fileOrIdentifier) ? fileOrIdentifier : juce::String());
    const auto stamp = f.getFullPathName().isEmpty() ? 0 : f.getLastModificationTime().toMilliseconds();
    {
        const std::scoped_lock l (lock);
        if (const auto it = cache.find (fileOrIdentifier); it != cache.end() && it->second.first == stamp)
            return it->second.second;
    }
    const auto is32 = moduleArch (fileOrIdentifier) == pearch::Arch::x86;
    const std::scoped_lock l (lock);
    cache[fileOrIdentifier] = { stamp, is32 };
    return is32;
}

bool isPlugin32Bit (const juce::PluginDescription& description)
{
    return (description.pluginFormatName == "VST" || description.pluginFormatName == "VST3") && isModule32Bit (description.fileOrIdentifier);
}


#if JUCE_WINDOWS || LYNX_POSIX_SANDBOX
namespace
{

class SandboxSession final : private juce::ChildProcessCoordinator
{
public:
    SandboxSession()
    {
        static std::atomic<int> counter { 0 };
       #if JUCE_WINDOWS
        const auto pid = (int) GetCurrentProcessId();
       #else
        const auto pid = (int) ::getpid();
       #endif
        baseName = "LYNX-sandbox-" + juce::String (pid) + "-" + juce::String (++counter);
        created = channel.create (baseName);
    }

    ~SandboxSession() override
    {
        closing = true;
        stopWatching();
        killWorkerProcess();
    }

    // Linux (round 161) and macOS: the moment the child process ends (a crash, abort, kill), the plugin is marked dead; before,
    // a crash was noticed only by the audio block's 0.5 s timeout or the connection's own ping
    void watchChild (int pid)
    {
       #if JUCE_LINUX && defined(SYS_pidfd_open)
        if (pid <= 0 || watcher.joinable())
            return;
        const int fd = (int) ::syscall (SYS_pidfd_open, (pid_t) pid, 0);
        if (fd < 0)
            return;
        stopFd = ::eventfd (0, EFD_CLOEXEC);
        if (stopFd < 0)
        {
            ::close (fd);
            return;
        }
        watcher = std::thread ([this, fd] {
            pollfd fds[2] = { { fd, POLLIN, 0 }, { stopFd, POLLIN, 0 } };
            while (::poll (fds, 2, -1) < 0 && errno == EINTR)
            {
            }
            if ((fds[0].revents & POLLIN) != 0)
            {
                trace ("parent: child process ended");
                markDead (true);
            }
            ::close (fd);
        });
       #elif JUCE_MAC
        // macOS: the same, through a kqueue watching the pid's exit (EVFILT_USER wakes it to stop)
        if (pid <= 0 || watcher.joinable())
            return;
        stopFd = ::kqueue();
        if (stopFd < 0)
            return;
        struct kevent changes[2];
        EV_SET (&changes[0], (uintptr_t) pid, EVFILT_PROC, EV_ADD | EV_ONESHOT, NOTE_EXIT, 0, nullptr);
        EV_SET (&changes[1], 1, EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0, nullptr);
        if (::kevent (stopFd, changes, 2, nullptr, 0, nullptr) != 0)
        {
            ::close (stopFd);
            stopFd = -1;
            return;
        }
        watcher = std::thread ([this] {
            struct kevent event {};
            while (::kevent (stopFd, nullptr, 0, &event, 1, nullptr) < 0 && errno == EINTR)
            {
            }
            if (event.filter == EVFILT_PROC)
            {
                trace ("parent: child process ended");
                markDead (true);
            }
        });
       #elif JUCE_WINDOWS
        // Windows (issue #159): the same through a thread-pool wait on the process handle. Before, a crash was noticed
        // only when JUCE's pipe read gave up waiting for the child to connect again (its 10 s timeout) or the ping
        // failed: until then every message to the child (a state save, a parameter, the close) waited those 10 s on
        // the pipe of a process that was gone.
        if (pid <= 0 || childWait != nullptr)
            return;
        childProcess = OpenProcess (SYNCHRONIZE, FALSE, (DWORD) pid);
        if (childProcess == nullptr)
            return;
        if (! RegisterWaitForSingleObject (&childWait, childProcess, &SandboxSession::childEnded, this, INFINITE, WT_EXECUTEONLYONCE))
        {
            childWait = nullptr;
            CloseHandle (childProcess);
            childProcess = nullptr;
        }
       #else
        juce::ignoreUnused (pid);
       #endif
    }

    bool start (const juce::File& executable)
    {
        if (! created)
            return false;
        const auto launched = launchWorkerProcess (executable, pluginSandboxId, 10000);
        trace ("parent: child launched " + juce::String ((int) launched));
        return launched;
    }

    const juce::String& name() const noexcept { return baseName; }
    SharedBlock* block() noexcept { return channel.block(); }
    void childAttached() { channel.unlinkName(); }
    bool isDead() const noexcept { return dead.load(); }

    // `crashed`: the process ended or its connection broke; else it stopped answering (a hang). Any thread (the audio
    // thread on a block's timeout): the death is only recorded (no allocation, no lock); the UI tells the user
    // (pluginSandboxDeaths, MainComponent).
    void markDead (bool crashed)
    {
        if (dead.exchange (true))
            return;
        replyReady.signal();
        channel.signalResponse(); // an audio thread waiting for a block that will never come returns now (round 161)
        if (! closing && showAlerts)
            recordDeath (nameCopy, crashed, bits32);
    }

    void setPluginName (const juce::String& n, bool is32)
    {
        pluginName = n;
        bits32 = is32;
        n.copyToUTF8 (nameCopy, sizeof (nameCopy));
    }
    void setPluginName (const juce::String& n) { pluginName = n; }

    // request with reply (UI thread)
    juce::ValueTree ask (const juce::ValueTree& message, int timeoutMs)
    {
        const std::scoped_lock lock (askMutex);
        if (dead)
            return {};
        {
            const std::scoped_lock l (replyMutex);
            reply = {};
        }
        replyReady.reset();
        if (! sendMessageToWorker (encode (message)))
            return {};
        if (! replyReady.wait (timeoutMs))
        {
            markDead (false);
            return {};
        }
        const std::scoped_lock l (replyMutex);
        return reply;
    }

    void tell (const juce::ValueTree& message)
    {
        if (! dead)
            sendMessageToWorker (encode (message));
    }

    // one audio block (audio thread): false = the child didn't respond
    bool runBlock (int timeoutMs) noexcept
    {
        if (dead)
            return false;
        channel.signalRequest();
        if (! channel.waitResponse (timeoutMs))
        {
            markDead (false);
            return false;
        }
        return ! dead; // woken by markDead (the child crashed or the connection was lost)
    }

private:
    void handleMessageFromWorker (const juce::MemoryBlock& data) override
    {
        auto message = juce::ValueTree::readFromData (data.getData(), data.getSize());
        if (message.hasType ("key")) // a key the plugin's window left unused (issue #99), never an ask() reply
        {
            const juce::KeyPress key ((int) message["code"], juce::ModifierKeys ((int) message["mods"]), (juce::juce_wchar) (int) message["char"]);
            juce::MessageManager::callAsync ([key] { runSecondaryWindowKey (key); });
            return;
        }
        {
            const std::scoped_lock l (replyMutex);
            reply = std::move (message);
        }
        trace ("parent: reply received " + reply.getType().toString());
        replyReady.signal();
    }

    void handleConnectionLost() override
    {
        trace ("parent: connection lost");
        markDead (true);
    }

   #if JUCE_WINDOWS
    static void CALLBACK childEnded (PVOID context, BOOLEAN /*timedOut*/)
    {
        trace ("parent: child process ended");
        static_cast<SandboxSession*> (context)->markDead (true);
    }
   #endif

    void stopWatching()
    {
       #if JUCE_WINDOWS
        if (childWait != nullptr)
            UnregisterWaitEx (childWait, INVALID_HANDLE_VALUE); // (waits for a callback in flight)
        childWait = nullptr;
        if (childProcess != nullptr)
            CloseHandle (childProcess);
        childProcess = nullptr;
       #endif
       #if JUCE_LINUX
        if (watcher.joinable())
        {
            const std::uint64_t one = 1;
            juce::ignoreUnused (::write (stopFd, &one, sizeof (one)));
            watcher.join();
        }
        if (stopFd >= 0)
            ::close (stopFd);
        stopFd = -1;
       #elif JUCE_MAC
        if (watcher.joinable())
        {
            struct kevent stop;
            EV_SET (&stop, 1, EVFILT_USER, 0, NOTE_TRIGGER, 0, nullptr);
            ::kevent (stopFd, &stop, 1, nullptr, 0, nullptr);
            watcher.join();
        }
        if (stopFd >= 0)
            ::close (stopFd);
        stopFd = -1;
       #endif
    }

    juce::String baseName, pluginName;
    char nameCopy[128] {};
    bool bits32 = false;
    SandboxChannel channel;
    bool created = false;
   #if JUCE_LINUX || JUCE_MAC
    std::thread watcher;
    int stopFd = -1;
   #elif JUCE_WINDOWS
    HANDLE childProcess = nullptr, childWait = nullptr;
   #endif
    std::atomic<bool> dead { false }, closing { false };
    std::mutex askMutex, replyMutex;
    juce::ValueTree reply;
    juce::WaitableEvent replyReady;
};

// Issue #159 (a user's hang report: the window froze for 10 s and more while a project was opened again): ending a
// session sends the child JUCE's "kill" message, and JUCE's pipe waits up to the connection timeout (10 s) for a child
// that is gone — crashed, or ended because the machine stalled — to connect again before it gives up. The rack frees
// plugins on the message thread (AudioEngine::RackDisposer), so every dead separate-process plugin of a project froze
// LYNX for 10 s when the project was imported again (Missing files › Apply) or closed. A session now ends on a thread
// of its own; nothing waits for it but the app's exit (finishSandboxTeardowns), and only briefly.
struct Retirements
{
    std::mutex lock;
    std::condition_variable done;
    int running = 0;
};
Retirements& retirements()
{
    static auto* r = new Retirements(); // (never destroyed: a teardown may still run while the process exits)
    return *r;
}

void retireSession (std::unique_ptr<SandboxSession> session)
{
    if (session == nullptr)
        return;
    auto& r = retirements();
    {
        const std::scoped_lock l (r.lock);
        ++r.running;
    }
    auto finished = [&r] {
        {
            const std::scoped_lock l (r.lock);
            --r.running;
        }
        r.done.notify_all();
    };
    try
    {
        std::thread ([raw = session.get(), finished] {
            juce::Thread::setCurrentThreadName ("LYNX sandbox teardown");
            delete raw;
            finished();
        }).detach();
        session.release(); // (the thread owns it now)
    }
    catch (...) // no thread to be had: here, as before
    {
        session.reset();
        finished();
    }
}

class RemotePlugin;

class RemoteParameter final : public juce::AudioPluginInstance::HostedParameter
{
public:
    RemoteParameter (RemotePlugin& o, int i, const juce::ValueTree& info)
        : owner (o), index (i),
          id (info["id"].toString()), name (info["name"].toString()), label (info["label"].toString()),
          defaultValue ((float) (double) info["default"]), steps ((int) info["steps"]), discrete ((bool) info["discrete"]),
          value ((float) (double) info["value"])
    {
    }

    float getValue() const override { return value.load(); }
    void setValue (float v) override;
    float getDefaultValue() const override { return defaultValue; }
    juce::String getName (int maximumLength) const override { return name.substring (0, maximumLength); }
    juce::String getLabel() const override { return label; }
    float getValueForText (const juce::String& text) const override { return juce::jlimit (0.0f, 1.0f, text.getFloatValue()); }
    juce::String getText (float v, int) const override { return juce::String (v, 3); }
    int getNumSteps() const override { return steps > 0 ? steps : AudioProcessorParameter::getNumSteps(); }
    bool isDiscrete() const override { return discrete; }
    juce::String getParameterID() const override { return id; }

    void setFromWorker (float v)
    {
        value = v;
        sendValueChangedMessageToListeners (v);
    }

private:
    RemotePlugin& owner;
    const int index;
    const juce::String id, name, label;
    const float defaultValue;
    const int steps;
    const bool discrete;
    std::atomic<float> value;
};

// LYNX's stand-in for a sandboxed plugin's editor (inside LYNX's plugin window). The plugin's own window lives in the
// child process: it is shown, owned by LYNX's plugin window, while this stand-in is on screen, and hidden when the stand-in
// is not (the Wrapper settings tab, the window closed or the plugin removed; issues #8 / #9).
class RemoteEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    RemoteEditor (juce::AudioProcessor& p, std::function<void (juce::int64)> show, std::function<void()> hide)
        : AudioProcessorEditor (p), showWindow (std::move (show)), hideWindow (std::move (hide))
    {
        text.setText (tr ("This plugin runs in a separate process: its interface opens in its own window."),
                      juce::dontSendNotification);
        text.setJustificationType (juce::Justification::centred);
        button.setButtonText (tr ("Show the plugin window"));
        button.onClick = [this] { showWindow (ownerHandle()); };
        addAndMakeVisible (text);
        addAndMakeVisible (button);
        setSize (380, 96);
        startTimerHz (4); // (a window shown, minimised or restored tells its children nothing: polled)
    }

    ~RemoteEditor() override
    {
        stopTimer();
        if (shown)
            hideWindow();
    }

    void visibilityChanged() override { follow(); }
    void timerCallback() override { follow(); }
    void parentHierarchyChanged() override { follow(); }

    void paint (juce::Graphics& g) override { g.fillAll (theme::colour::bg1); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8);
        button.setBounds (r.removeFromBottom (28).withSizeKeepingCentre (220, 28));
        text.setBounds (r);
    }

private:
    // LYNX's main window (HWND, X11 Window id): the child's window is kept above it, not above every program. (Not the
    // plugin window holding this stand-in: Windows destroys an owned window with its owner, even in another process.)
    juce::int64 ownerHandle() { return (juce::int64) (juce::pointer_sized_int) mainWindowHandle(); }

    // A LYNX dialog (Save changes?, Export...) is open: the child's window floats above LYNX's windows while LYNX is
    // active, so it would cover the dialog. Popup menus are modal too, but aren't TopLevelWindows: they don't hide it.
    // shortcut: native dialogs (file choosers) aren't JUCE components and aren't seen; upgrade if one is covered.
    static bool dialogOpen()
    {
        for (int i = juce::Component::getNumCurrentlyModalComponents(); --i >= 0;)
            if (dynamic_cast<juce::TopLevelWindow*> (juce::Component::getCurrentlyModalComponent (i)) != nullptr)
                return true;
        return false;
    }

    void follow()
    {
        const auto showing = isShowing() && getPeer() != nullptr && ! dialogOpen();
        if (showing == shown)
            return;
        shown = showing;
        if (shown)
            showWindow (ownerHandle());
        else
            hideWindow();
    }

    std::function<void (juce::int64)> showWindow;
    std::function<void()> hideWindow;
    bool shown = false;
    juce::Label text;
    juce::TextButton button;
};

class RemotePlugin final : public juce::AudioPluginInstance
{
public:
    RemotePlugin (std::unique_ptr<SandboxSession> s, const juce::PluginDescription& d, const juce::ValueTree& info, bool is32)
        : AudioPluginInstance (busesFor ((int) info["ins"], (int) info["outs"])), bits32 (is32), pid ((int) info["pid"]),
          session (std::move (s)), description (d),
          midiIn ((bool) info["midiIn"]), midiOut ((bool) info["midiOut"]), editor ((bool) info["editor"]),
          tail ((double) info["tail"]), lastState (info["state"].toString())
    {
        session->setPluginName (description.name, is32);
        setLatencySamples ((int) info["latency"]);
        const auto params = info.getChildWithName ("params");
        for (int i = 0; i < params.getNumChildren(); ++i)
        {
            auto p = std::make_unique<RemoteParameter> (*this, i, params.getChild (i));
            remoteParams.push_back (p.get());
            addHostedParameter (std::move (p));
        }
    }

    ~RemotePlugin() override { retireSession (std::move (session)); } // (never waits here: issue #159)

    bool dead() const noexcept { return session->isDead(); }
    const bool bits32;
    const int pid;
    void hangForTest() { session->tell (juce::ValueTree ("hang")); }
    void crashForTest() { session->tell (juce::ValueTree ("crash")); }

    void queueParameter (int index, float value) noexcept { pending.push (index, value); }

    void fillInPluginDescription (juce::PluginDescription& d) const override { d = description; }
    const juce::String getName() const override { return description.name; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    // an export renders (round 168s, issue #32: bridged plugins stayed in real-time mode, with their old voices and a
    // 0.5 s deadline per block): the worker takes the mode and the reset with the next block, on its audio thread
    void setNonRealtime (bool offline) noexcept override
    {
        AudioPluginInstance::setNonRealtime (offline);
        pendingMode.store (offline ? 2 : 1, std::memory_order_release);
    }
    void reset() override { pendingReset.store (1, std::memory_order_release); }
    double getTailLengthSeconds() const override { return tail; }
    bool acceptsMidi() const override { return midiIn; }
    bool producesMidi() const override { return midiOut; }
    bool hasEditor() const override { return editor; }
    juce::AudioProcessorEditor* createEditor() override
    {
        return new RemoteEditor (
            *this, [this] (juce::int64 owner) { session->tell (juce::ValueTree ("showEditor").setProperty ("owner", owner, nullptr)); },
            [this] { session->tell (juce::ValueTree ("hideEditor")); });
    }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& dest) override
    {
        if (const auto r = session->ask (juce::ValueTree ("getState"), 10000); r.isValid())
            lastState = r["state"].toString();
        dest.fromBase64Encoding (lastState);
    }

    void setStateInformation (const void* data, int size) override
    {
        const juce::MemoryBlock block (data, (std::size_t) juce::jmax (0, size));
        lastState = block.toBase64Encoding();
        session->ask (juce::ValueTree ("setState").setProperty ("state", lastState, nullptr), 10000);
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        auto* shared = session->block();
        const auto frames = std::min (buffer.getNumSamples(), SharedBlock::maxFrames);
        const auto channels = std::min (buffer.getNumChannels(), SharedBlock::maxChannels);
        if (session->isDead() || shared == nullptr || frames <= 0)
        {
            buffer.clear();
            return;
        }

        shared->frames = frames;
        shared->channels = channels;
        for (int c = 0; c < channels; ++c)
            std::memcpy (shared->audio[c], buffer.getReadPointer (c), sizeof (float) * (std::size_t) frames);

        int count = 0;
        for (const auto m : midi)
            if (m.numBytes >= 1 && m.numBytes <= 3 && count < SharedBlock::maxMidi)
            {
                auto& e = shared->midi[count++];
                e.offset = juce::jlimit (0, frames - 1, m.samplePosition);
                e.size = (std::uint8_t) m.numBytes;
                std::memcpy (e.bytes, m.data, (std::size_t) m.numBytes);
            }
        shared->midiCount = count;
        shared->paramCount = pending.drain (shared->params, SharedBlock::maxParams);
        shared->paramOutCount = 0;
        if (auto* head = getPlayHead())
            writeTransport (shared->transport, head->getPosition());
        else
            shared->transport = {};
        shared->mode = pendingMode.exchange (0, std::memory_order_acq_rel);
        shared->resetFirst = pendingReset.exchange (0, std::memory_order_acq_rel);

        // timeout: 0.5 s (a healthy plugin responds in well under one block); an export waits for the plugin (a block
        // dropped there was a gap in the file)
        if (! session->runBlock (isNonRealtime() ? 30000 : 500))
        {
            buffer.clear();
            return;
        }

        for (int c = 0; c < channels; ++c)
            std::memcpy (buffer.getWritePointer (c), shared->audio[c], sizeof (float) * (std::size_t) frames);
        for (int c = channels; c < buffer.getNumChannels(); ++c)
            buffer.clear (c, 0, frames);

        for (int i = 0; i < std::min (shared->paramOutCount, SharedBlock::maxParams); ++i)
            if (const auto k = shared->paramsOut[i].index; k >= 0 && k < (int) remoteParams.size())
                remoteParams[(std::size_t) k]->setFromWorker (shared->paramsOut[i].value);

        if (shared->latency != getLatencySamples())
            setLatencySamples (shared->latency);
    }

private:
    std::atomic<int> pendingMode { 0 }, pendingReset { 0 };
    static BusesProperties busesFor (int ins, int outs)
    {
        auto set = [] (int n) { return n <= 8 ? juce::AudioChannelSet::canonicalChannelSet (n) : juce::AudioChannelSet::discreteChannels (n); };
        BusesProperties buses;
        if (ins > 0)
            buses = buses.withInput ("Input", set (ins), true);
        if (outs > 0)
            buses = buses.withOutput ("Output", set (outs), true);
        return buses;
    }

    std::unique_ptr<SandboxSession> session;
    juce::PluginDescription description;
    bool midiIn, midiOut, editor;
    double tail;
    juce::String lastState;
    std::vector<RemoteParameter*> remoteParams;
    ParamQueue pending;
};

void RemoteParameter::setValue (float v)
{
    value = v;
    owner.queueParameter (index, v);
}

} // namespace

std::unique_ptr<juce::AudioPluginInstance> createSandboxedPlugin (const juce::PluginDescription& description, double sampleRate,
                                                                  int blockSize, const std::string& stateBase64, juce::String& error)
{
    trace ("parent: session");
    // a 32-bit plugin: the 32-bit bridge (round 176a); every other plugin: LYNX.exe itself in child mode
    const auto bridged32 = isPlugin32Bit (description);
    const auto executable = bridged32 ? bridge32Executable() : juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    if (bridged32 && ! executable.existsAsFile())
    {
        error = juce::String::fromUTF8 ("a 32-bit plugin, and the 32-bit bridge (LYNX-bridge32.exe) is missing");
        return nullptr;
    }
    auto session = std::make_unique<SandboxSession>();
    session->setPluginName (description.name, bridged32);
    trace ("parent: starting child " + executable.getFileName());
    if (! session->start (executable))
    {
        error = juce::String::fromUTF8 (bridged32 ? "could not start the 32-bit bridge (LYNX-bridge32.exe)" : "could not start the plugin process");
        return nullptr;
    }

    juce::ValueTree open ("open");
    open.setProperty ("shm", session->name(), nullptr);
    open.setProperty ("description", description.createXml()->toString(), nullptr);
    open.setProperty ("sampleRate", sampleRate, nullptr);
    open.setProperty ("blockSize", blockSize, nullptr);
    open.setProperty ("state", juce::String::fromUTF8 (stateBase64.c_str()), nullptr);

    trace ("parent: open");
    const auto info = session->ask (open, 30000);
    session->childAttached();
    if (info.isValid())
        session->watchChild ((int) info["pid"]);
    trace ("parent: reply " + juce::String ((int) info.isValid()));
    if (! info.isValid() || ! (bool) info["ok"])
    {
        error = info.isValid() ? info["error"].toString() : juce::String::fromUTF8 ("the plugin process did not respond");
        return nullptr;
    }
    auto remote = std::make_unique<RemotePlugin> (std::move (session), description, info, bridged32);
    trace ("parent: proxy ready");
    return remote;
}

bool finishSandboxTeardowns (int timeoutMs)
{
    auto& r = retirements();
    std::unique_lock l (r.lock);
    return r.done.wait_for (l, std::chrono::milliseconds (std::max (0, timeoutMs)), [&r] { return r.running == 0; });
}

int sandboxTeardownsRunning()
{
    auto& r = retirements();
    const std::scoped_lock l (r.lock);
    return r.running;
}

bool isSandboxedPluginDead (const juce::AudioPluginInstance& plugin)
{
    const auto* remote = dynamic_cast<const RemotePlugin*> (&plugin);
    return remote != nullptr && remote->dead();
}

void hangSandboxedPluginForTest (juce::AudioPluginInstance& plugin)
{
    if (auto* remote = dynamic_cast<RemotePlugin*> (&plugin))
        remote->hangForTest();
}

void crashSandboxedPluginForTest (juce::AudioPluginInstance& plugin)
{
    if (auto* remote = dynamic_cast<RemotePlugin*> (&plugin))
        remote->crashForTest();
}

bool isBridged32 (const juce::AudioPluginInstance& plugin)
{
    const auto* remote = dynamic_cast<const RemotePlugin*> (&plugin);
    return remote != nullptr && remote->bits32;
}

bool isSandboxedPlugin (const juce::AudioPluginInstance& plugin)
{
    return dynamic_cast<const RemotePlugin*> (&plugin) != nullptr;
}

int sandboxedPluginProcessId (const juce::AudioPluginInstance& plugin)
{
    const auto* remote = dynamic_cast<const RemotePlugin*> (&plugin);
    return remote != nullptr ? remote->pid : 0;
}

#else // other platforms: not yet

std::unique_ptr<juce::AudioPluginInstance> createSandboxedPlugin (const juce::PluginDescription&, double, int, const std::string&, juce::String& error)
{
    error = juce::String::fromUTF8 ("out-of-process plugins: Windows, macOS and Linux only");
    return nullptr;
}

bool isSandboxedPluginDead (const juce::AudioPluginInstance&) { return false; }
bool finishSandboxTeardowns (int) { return true; }
int sandboxTeardownsRunning() { return 0; }
bool isBridged32 (const juce::AudioPluginInstance&) { return false; }
int sandboxedPluginProcessId (const juce::AudioPluginInstance&) { return 0; }
bool isSandboxedPlugin (const juce::AudioPluginInstance&) { return false; }
void hangSandboxedPluginForTest (juce::AudioPluginInstance&) {}
void crashSandboxedPluginForTest (juce::AudioPluginInstance&) {}

#endif
} // namespace lynx
