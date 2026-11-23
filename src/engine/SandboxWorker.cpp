#include "engine/SandboxWorker.h"

#include <map>
#include <optional>

#include <juce_gui_extra/juce_gui_extra.h>

#include "core/Denormals.h"
#include "platform/NativeEmbed.h"
#include "platform/PowerThrottling.h"
#include "engine/PluginSandbox.h"
#include "engine/SandboxShared.h"
#include "engine/TimedParameters.h"

#if JUCE_LINUX
 #include <dlfcn.h>
#elif JUCE_MAC
 #include <CoreGraphics/CoreGraphics.h>
 #include <objc/message.h>
 #include <objc/runtime.h>
#endif

// The child side of the plugin sandbox (split out of PluginSandbox.cpp in round 176a): the process that hosts plugins
// for LYNX — LYNX.exe --plugin-host, or the 32-bit bridge LYNX-bridge32.exe for 32-bit plugins. One plugin per process,
// or (round 190jg) a group of them: every message names its plugin's slot.

namespace lynx
{
using namespace sandboxipc;

#if JUCE_WINDOWS || LYNX_POSIX_SANDBOX
namespace
{
#if JUCE_MAC
// the pid of the active app ([NSWorkspace sharedWorkspace].frontmostApplication.processIdentifier, from C++)
pid_t frontmostPid()
{
    const auto send = (id (*) (id, SEL)) objc_msgSend;
    const id workspace = send ((id) objc_getClass ("NSWorkspace"), sel_registerName ("sharedWorkspace"));
    const id app = workspace != nil ? send (workspace, sel_registerName ("frontmostApplication")) : nil;
    return app != nil ? ((pid_t (*) (id, SEL)) objc_msgSend) (app, sel_registerName ("processIdentifier")) : 0;
}

// the window number of LYNX's frontmost normal window on screen (0: none)
long lynxFrontWindow()
{
    long number = 0;
    if (const auto list = CGWindowListCopyWindowInfo (kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements, kCGNullWindowID))
    {
        auto get = [] (CFDictionaryRef d, CFStringRef key) {
            long v = -1;
            if (const auto n = (CFNumberRef) CFDictionaryGetValue (d, key))
                CFNumberGetValue (n, kCFNumberLongType, &v);
            return v;
        };
        for (CFIndex i = 0; i < CFArrayGetCount (list) && number == 0; ++i) // front to back
        {
            const auto d = (CFDictionaryRef) CFArrayGetValueAtIndex (list, i);
            if (get (d, kCGWindowOwnerPID) == ::getppid() && get (d, kCGWindowLayer) == 0)
                number = get (d, kCGWindowNumber);
        }
        CFRelease (list);
    }
    return number;
}
#endif

class SandboxWindow final : public juce::DocumentWindow
{
public:
    explicit SandboxWindow (const juce::String& title)
        : DocumentWindow (title, sandboxhost::windowColour(), DocumentWindow::closeButton | DocumentWindow::minimiseButton)
    {
        setUsingNativeTitleBar (true);
        setIcon (sandboxhost::windowIcon());
    }

    // Keeps the window above LYNX's main window (`owner`: its HWND / X11 Window id, from the other process) instead of
    // above every program: it was always on top, covering LYNX's Wrapper settings and other apps (issue #8). False when
    // the platform can't (then it stays always on top, as before). Windows: not done — an owner in another process
    // attaches the two processes' input queues, so a plugin that hangs its window would freeze LYNX's too; there the
    // window stays on top but is hidden whenever LYNX's stand-in is not on screen (Wrapper settings, minimised, closed).
    // macOS: no owner across processes either, so the window follows which app is active instead.
    bool setOwner (juce::int64 owner)
    {
        if (owner == 0 || getWindowHandle() == nullptr)
            return false;
       #if JUCE_LINUX
        // WM_TRANSIENT_FOR across processes (X11 / XWayland), through our own connection to the display
        using OpenDisplay = void* (*) (const char*);
        using SetTransient = int (*) (void*, unsigned long, unsigned long);
        using Flush = int (*) (void*);
        static void* lib = dlopen ("libX11.so.6", RTLD_NOW | RTLD_LOCAL);
        if (lib == nullptr)
            return false;
        static auto* openDisplay = (OpenDisplay) dlsym (lib, "XOpenDisplay");
        static auto* setTransient = (SetTransient) dlsym (lib, "XSetTransientForHint");
        static auto* flush = (Flush) dlsym (lib, "XFlush");
        static void* display = openDisplay != nullptr ? openDisplay (nullptr) : nullptr;
        if (display == nullptr || setTransient == nullptr || flush == nullptr)
            return false;
        setTransient (display, (unsigned long) (juce::pointer_sized_int) getWindowHandle(), (unsigned long) owner);
        flush (display);
        return true;
       #elif JUCE_MAC
        // no cross-process owner on macOS: the window floats only while LYNX or this child is the active app, and
        // drops to a normal window (behind the other app) otherwise
        // shortcut: polled 4x a second, upgrade to NSWorkspaceDidActivateApplicationNotification in an .mm if it lags
        followLynx.startTimer (250);
        return true;
       #else
        return false;
       #endif
    }

    void closeButtonPressed() override { setVisible (false); }

    // A key the plugin's editor left unused (issue #99): LYNX never sees this process's keys, so it goes to LYNX's
    // shortcuts (Space plays, as with an in-process plugin window); Esc closes the window, as in LYNX's other windows.
    // Always used here: macOS would otherwise beep for every key.
    // shortcut: keys typed into a plugin's native child view on Windows never reach JUCE, so they still stay in the
    // plugin; upgrade with a keyboard hook in this process (as pluginKeyProc in WindowOwner.cpp) if that is wanted.
    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == heldKey) // an autorepeat: holding Space would toggle playback on and off (pluginKeyProc skips them too)
            return true;
        heldKey = key;
        if (key == juce::KeyPress::escapeKey)
            closeButtonPressed();
        else if (onUnusedKey != nullptr)
            onUnusedKey (key);
        return true;
    }

    bool keyStateChanged (bool) override
    {
        if (! juce::KeyPress::isKeyCurrentlyDown (heldKey.getKeyCode()))
            heldKey = {}; // released: the next press of it is a new one
        return false;
    }

    std::function<void (const juce::KeyPress&)> onUnusedKey;
    juce::KeyPress heldKey;

   #if JUCE_MAC
private:
    juce::TimedCallback followLynx { [this] {
        const auto front = frontmostPid();
        const auto active = front == ::getppid() || front == ::getpid();
        if (active == isAlwaysOnTop())
            return;
        setAlwaysOnTop (active);
        // dropped to a normal window: macOS would leave it below LYNX's main window, so it goes back just above it
        if (const auto lynx = lynxFrontWindow(); ! active && lynx != 0 && getWindowHandle() != nullptr)
            if (const id window = ((id (*) (id, SEL)) objc_msgSend) ((id) getWindowHandle(), sel_registerName ("window")))
                ((void (*) (id, SEL, long, long)) objc_msgSend) (window, sel_registerName ("orderWindow:relativeTo:"), 1 /* NSWindowAbove */, lynx);
    } };
   #endif
};

// The bridge's x87 control word before plugin calls (32-bit x86 only: an old plugin's x87 maths follows it; LYNX's own
// 64-bit code never uses x87). FL's ilbridge.exe resets it the same way (REA_rodada68 §2.1, REA_rodada176a §3).
void applyFloatMode() noexcept
{
   #if defined(__i386__) && defined(__GNUC__)
    if (const auto cw = (unsigned short) sandboxhost::x87ControlWord(); cw != 0)
    {
        unsigned short now = 0;
        __asm__ volatile ("fnstcw %0" : "=m" (now));
        if (now != cw)
            __asm__ volatile ("fldcw %0" : : "m" (cw));
    }
   #endif
}

class SandboxWorker;

// One plugin of the process (round 190jg: a sandbox process may host several, Bitwig-style groups: per vendor or all
// plugins together). Each has its own shared block and doorbells and its own audio thread, so plugins of one group
// run as independently as in separate processes; only a crash takes the whole group.
class SandboxSlot final : private juce::Thread,
                          private juce::AudioProcessorParameter::Listener,
                          private juce::AudioPlayHead
{
public:
    SandboxSlot (SandboxWorker& w, int i) : Thread ("LYNX sandbox audio"), worker (w), slot (i) {}

    ~SandboxSlot() override
    {
        stopThread (2000);
        window.reset();
        if (plugin != nullptr)
            for (auto* p : plugin->getParameters())
                p->removeListener (this);
        plugin.reset();
    }

    juce::ValueTree open (const juce::ValueTree& message);
    void handle (const juce::ValueTree& message);

private:
    void run() override
    {
        // the sandbox's audio thread gets the same FTZ/DAZ as LYNX's own audio threads (round 160: it had none)
        const lynx::ScopedFlushDenormals noDenormals;
        // Windows: MMCSS "Pro Audio" as LYNX's own audio threads, and no EcoQoS while LYNX is in the background (a
        // sandbox or bridge process inherits the window owner's Low QoS; round toolchain190gt)
        lynx::power::becomeProAudioThread();
        while (! threadShouldExit())
        {
            if (! channel.waitRequest (100))
                continue;
            applyFloatMode();
            process();
            channel.signalResponse();
        }
    }

    void process() noexcept
    {
        if (hang) // only in the timeout test
            Thread::sleep (3000);
        if (shared->mode == 1 || shared->mode == 2)
        {
            plugin->setNonRealtime (shared->mode == 2);
            sandboxhost::prepare (*plugin, sampleRate, blockSize); // (a VST3 takes the process mode when it is set up)
        }
        if (shared->resetFirst != 0)
            plugin->reset();
        const auto frames = juce::jlimit (0, blockSize, (int) shared->frames);
        const auto& list = plugin->getParameters();
        for (int i = 0; i < std::min ((int) shared->paramCount, SharedBlock::maxParams); ++i)
            if (const auto k = shared->params[i].index; k >= 0 && k < list.size())
                list[k]->setValue (shared->params[i].value);

        midiBuffer.clear();
        for (int i = 0; i < std::min ((int) shared->midiCount, SharedBlock::maxMidi); ++i)
        {
            const auto& e = shared->midi[i];
            if (e.size >= 1 && e.size <= 3)
                midiBuffer.addEvent (e.bytes, e.size, juce::jlimit (0, juce::jmax (0, frames - 1), (int) e.offset));
        }

        float* pointers[SharedBlock::maxChannels];
        for (int c = 0; c < channels; ++c)
        {
            pointers[c] = shared->audio[c];
            if (c >= shared->channels)
                std::memset (shared->audio[c], 0, sizeof (float) * (std::size_t) frames);
        }
        // round 190jg: the parent's timed parameter changes, for the plugin's host (engine/TimedParameters.h)
        const auto timed = juce::jlimit (0, SharedBlock::maxTimed, (int) shared->timedCount);
        for (int k = 0; k < timed; ++k)
        {
            timedIndices[(std::size_t) k] = shared->timed[k].index;
            timedValues[(std::size_t) k] = shared->timed[k].value;
            timedOffsets[(std::size_t) k] = shared->timedOffsets[k];
        }
        juce::lynxTimedParamCount = timed;
        juce::lynxTimedParamIndices = timedIndices.data();
        juce::lynxTimedParamOffsets = timedOffsets.data();
        juce::lynxTimedParamValues = timedValues.data();
        if (frames > 0)
        {
            juce::AudioBuffer<float> buffer (pointers, channels, frames);
            plugin->processBlock (buffer, midiBuffer);
        }
        juce::lynxTimedParamCount = 0;
        // (rollup 0.1.30) what the plugin sent out, for the parent's buffer
        int outCount = 0;
        if (frames > 0)
            for (const auto m : midiBuffer)
                if (m.numBytes >= 1 && m.numBytes <= 3 && outCount < SharedBlock::maxMidi)
                {
                    auto& e = shared->midiOut[outCount++];
                    e.offset = juce::jlimit (0, frames - 1, m.samplePosition);
                    e.size = (std::uint8_t) m.numBytes;
                    std::memcpy (e.bytes, m.data, (std::size_t) m.numBytes);
                }
        shared->midiOutCount = outCount;

        shared->paramOutCount = changed.drain (shared->paramsOut, SharedBlock::maxParams);
        shared->latency = plugin->getLatencySamples();
    }

    // the plugin's play head: the parent's position for the block being processed (only read inside processBlock)
    juce::Optional<PositionInfo> getPosition() const override { return shared != nullptr ? readTransport (shared->transport) : juce::Optional<PositionInfo>(); }

    void parameterValueChanged (int index, float value) override { changed.push (index, value); }
    void parameterGestureChanged (int, bool) override {}

    SandboxWorker& worker;
    const int slot;
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    std::unique_ptr<SandboxWindow> window;
    SandboxChannel channel;
    SharedBlock* shared = nullptr;
    int blockSize = 512, channels = 2;
    double sampleRate = 44100.0;
    std::atomic<bool> hang { false };
    juce::MidiBuffer midiBuffer;
    ParamQueue changed;
    std::array<int, SharedBlock::maxTimed> timedIndices {}, timedOffsets {};
    std::array<float, SharedBlock::maxTimed> timedValues {};
};

class SandboxWorker final : public juce::ChildProcessWorker
{
public:
    ~SandboxWorker() override { slots.clear(); }

    void handleMessageFromCoordinator (const juce::MemoryBlock& data) override
    {
        auto message = juce::ValueTree::readFromData (data.getData(), data.getSize());
        trace ("child: message " + message.getType().toString() + " slot " + message["slot"].toString());
        juce::MessageManager::callAsync ([this, message] { handle (message); });
    }

    void handleConnectionLost() override
    {
        trace ("child: connection lost");
        juce::MessageManager::callAsync ([] { juce::JUCEApplicationBase::quit(); });
    }

    // a reply to the parent's request (its sequence number goes back with it: a late reply is never taken for another)
    void reply (const juce::ValueTree& request, juce::ValueTree message)
    {
        message.setProperty ("seq", request["seq"], nullptr);
        send (message);
    }

    void send (const juce::ValueTree& message) { sendMessageToCoordinator (encode (message)); }

private:
    void handle (const juce::ValueTree& message)
    {
        applyFloatMode();
        const auto slot = (int) message.getProperty ("slot", 0);
        if (message.hasType ("open"))
        {
            auto created = std::make_unique<SandboxSlot> (*this, slot);
            auto answer = created->open (message);
            if ((bool) answer["ok"])
                slots[slot] = std::move (created);
            reply (message, answer);
            return;
        }
        if (message.hasType ("close")) // (round 190jg) one plugin of the group goes; the process stays for the others
        {
            slots.erase (slot);
            return;
        }
        if (message.hasType ("crash")) // only in the crash-isolation test
            std::abort();
        if (const auto it = slots.find (slot); it != slots.end())
        {
            it->second->handle (message);
            return;
        }
        if (message.hasType ("getState") || message.hasType ("setState")) // (a slot that never opened: still answered)
            reply (message, juce::ValueTree (message.hasType ("getState") ? "state" : "ok"));
    }

    std::map<int, std::unique_ptr<SandboxSlot>> slots;
};

void SandboxSlot::handle (const juce::ValueTree& message)
{
    if (message.hasType ("getState"))
    {
        juce::ValueTree answer ("state");
        if (plugin != nullptr)
            answer.setProperty ("state", juce::String (sandboxhost::captureState (*plugin)), nullptr);
        worker.reply (message, answer);
    }
    else if (message.hasType ("setState"))
    {
        if (plugin != nullptr)
        {
            juce::MemoryBlock state;
            if (state.fromBase64Encoding (message["state"].toString()))
                plugin->setStateInformation (state.getData(), (int) state.getSize());
        }
        worker.reply (message, juce::ValueTree ("ok"));
    }
    else if (message.hasType ("hang"))
        hang = true;
    else if (message.hasType ("showEditor") && plugin != nullptr && plugin->hasEditor())
    {
        // FL's wrapper Settings › GUI (round 190oh, P3), as in LYNX's own process (ui/PluginWindow.cpp): "DPI aware" off
        // makes the window and the editor DPI-unaware (Windows), "Scale editor dimensions" sizes it in logical pixels
       #if JUCE_WINDOWS
        const auto unaware = ! (bool) message.getProperty ("dpiAware", true);
       #else
        const auto unaware = false;
       #endif
        const auto scaleDims = (bool) message.getProperty ("scaleDims", false);
        std::optional<juce::ScopedDPIAwarenessDisabler> dpi;
        if (unaware)
            dpi.emplace();
        if (window == nullptr)
        {
            window = std::make_unique<SandboxWindow> (plugin->getName());
            window->onUnusedKey = [this] (const juce::KeyPress& key) {
                worker.send (juce::ValueTree ("key").setProperty ("code", key.getKeyCode(), nullptr)
                                                    .setProperty ("mods", key.getModifiers().getRawFlags(), nullptr)
                                                    .setProperty ("char", (int) key.getTextCharacter(), nullptr));
            };
            if (auto* ed = plugin->createEditorIfNeeded())
            {
                if (unaware || scaleDims)
                    platform::setEditorScaling (*ed, unaware, scaleDims);
                window->setContentOwned (ed, true);
            }
            window->centreWithSize (window->getWidth(), window->getHeight());
        }
        if (! window->isOnDesktop())
            window->addToDesktop(); // (the native window exists before it is shown: its owner is set first)
        window->setAlwaysOnTop (! window->setOwner ((juce::int64) message["owner"]));
        window->setVisible (true);
        window->toFront (true);
    }
    else if (message.hasType ("hideEditor") && window != nullptr)
        window->setVisible (false);
}

juce::ValueTree SandboxSlot::open (const juce::ValueTree& message)
{
    juce::ValueTree reply ("opened");
    auto fail = [&] (const juce::String& why) {
        reply.setProperty ("error", why, nullptr);
        return reply;
    };

    const auto base = message["shm"].toString();
    if (! channel.open (base))
        return fail ("shared memory unavailable");
    shared = channel.block();

    juce::PluginDescription description;
    const auto xml = juce::parseXML (message["description"].toString());
    if (xml == nullptr || ! description.loadFromXml (*xml))
        return fail ("invalid plugin description");

    sampleRate = (double) message["sampleRate"];
    blockSize = juce::jlimit (16, SharedBlock::maxFrames, (int) message["blockSize"]);
    juce::String error;
    applyFloatMode();
    plugin = sandboxhost::open (description, sampleRate, blockSize, error);
    if (plugin == nullptr)
        return fail (error.isEmpty() ? juce::String ("the plugin failed to open") : error);

    plugin->setPlayHead (this); // the host's position of each block (SharedBlock::transport)
    sandboxhost::prepare (*plugin, sampleRate, blockSize);
    if (const auto state = message["state"].toString(); state.isNotEmpty())
    {
        juce::MemoryBlock block;
        if (block.fromBase64Encoding (state))
            plugin->setStateInformation (block.getData(), (int) block.getSize());
    }

    reply.setProperty ("ins", plugin->getTotalNumInputChannels(), nullptr);
    reply.setProperty ("outs", plugin->getTotalNumOutputChannels(), nullptr);
    reply.setProperty ("midiIn", plugin->acceptsMidi(), nullptr);
    reply.setProperty ("midiOut", plugin->producesMidi(), nullptr);
    reply.setProperty ("editor", plugin->hasEditor(), nullptr);
    reply.setProperty ("tail", plugin->getTailLengthSeconds(), nullptr);
    reply.setProperty ("latency", plugin->getLatencySamples(), nullptr);
   #if LYNX_POSIX_SANDBOX
    reply.setProperty ("pid", (int) ::getpid(), nullptr);
   #elif JUCE_WINDOWS
    reply.setProperty ("pid", (int) GetCurrentProcessId(), nullptr);
   #endif
    reply.setProperty ("state", juce::String (sandboxhost::captureState (*plugin)), nullptr);

    juce::ValueTree params ("params");
    const auto& list = plugin->getParameters();
    for (int i = 0; i < list.size(); ++i)
    {
        auto* p = list[i];
        juce::ValueTree info ("p");
        info.setProperty ("name", p->getName (128), nullptr);
        info.setProperty ("label", p->getLabel(), nullptr);
        info.setProperty ("default", p->getDefaultValue(), nullptr);
        info.setProperty ("value", p->getValue(), nullptr);
        info.setProperty ("steps", p->getNumSteps(), nullptr);
        info.setProperty ("discrete", p->isDiscrete(), nullptr);
        if (auto* hosted = dynamic_cast<juce::HostedAudioProcessorParameter*> (p))
            info.setProperty ("id", hosted->getParameterID(), nullptr);
        else
            info.setProperty ("id", juce::String (i), nullptr);
        params.appendChild (info, nullptr);
        p->addListener (this);
    }
    reply.appendChild (params, nullptr);

    channels = juce::jmin (SharedBlock::maxChannels, juce::jmax (2, plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels()));
    midiBuffer.ensureSize (8192);
    startThread (juce::Thread::Priority::highest);
    reply.setProperty ("ok", true, nullptr);
    return reply;
}

std::unique_ptr<SandboxWorker>& worker()
{
    static std::unique_ptr<SandboxWorker> w;
    return w;
}
} // namespace

bool runPluginSandboxWorker (const juce::String& commandLine)
{
    if (! commandLine.contains (pluginSandboxId))
        return false;
    trace ("child: " + commandLine);
   #if JUCE_MAC
    juce::Process::setDockIconVisible (false); // (one LYNX icon in the Dock, not one more per sandboxed plugin)
   #endif
    worker() = std::make_unique<SandboxWorker>();
    if (! worker()->initialiseFromCommandLine (commandLine, pluginSandboxId, 10000))
    {
        worker().reset();
        return false;
    }
    return true;
}

void shutdownPluginSandboxWorker()
{
    worker().reset();
}
#else
bool runPluginSandboxWorker (const juce::String&) { return false; }
void shutdownPluginSandboxWorker() {}
#endif
} // namespace lynx
