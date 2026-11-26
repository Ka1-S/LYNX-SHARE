// Round 190rp: audio device opens with a time limit (engine/DeviceGuard.h).

#include "engine/DeviceGuard.h"

#include "platform/Paths.h"

#include <atomic>
#include <mutex>
#include <set>
#include <thread>

#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace lynx::deviceguard
{
namespace
{
struct Registry
{
    std::mutex lock;
    std::set<std::pair<juce::String, juce::String>> hung, again;
    juce::String last;
    int count = 0;
};
Registry& registry()
{
    static auto* r = new Registry(); // (never destroyed: workers that come back late may still look)
    return *r;
}

void markHung (const juce::String& type, const juce::String& device)
{
    auto& r = registry();
    const std::lock_guard<std::mutex> l (r.lock);
    r.hung.insert ({ type, device });
    r.again.erase ({ type, device });
    r.last = type + ": " + device;
    ++r.count;
}

// a call of the driver on a worker, waited for up to the time limit; false when it did not come back (the worker then
// keeps going on its own, with everything it uses held by `state`)
template <typename State>
bool runBounded (std::shared_ptr<State> state, std::function<void (State&)> work)
{
    std::thread ([state, work] {
        work (*state);
        state->done.signal();
    }).detach();
    return waitAnsweringSentMessages (state->done, timeoutMs());
}

class GuardedDevice final : public juce::AudioIODevice
{
public:
    GuardedDevice (std::shared_ptr<juce::AudioIODevice> d, const juce::String& name, const juce::String& type, std::shared_ptr<std::atomic<bool>> typeAbandoned)
        : juce::AudioIODevice (name, type), inner (std::move (d)), abandonedInType (std::move (typeAbandoned))
    {
    }
    ~GuardedDevice() override
    {
        if (! abandoned && inner != nullptr && inner.use_count() == 1)
            inner.reset(); // (deleted here, as without the guard)
    }

    juce::StringArray getOutputChannelNames() override { return usable() ? inner->getOutputChannelNames() : juce::StringArray(); }
    juce::StringArray getInputChannelNames() override { return usable() ? inner->getInputChannelNames() : juce::StringArray(); }
    std::optional<juce::BigInteger> getDefaultOutputChannels() const override { return usable() ? inner->getDefaultOutputChannels() : std::nullopt; }
    std::optional<juce::BigInteger> getDefaultInputChannels() const override { return usable() ? inner->getDefaultInputChannels() : std::nullopt; }
    juce::Array<double> getAvailableSampleRates() override { return usable() ? inner->getAvailableSampleRates() : juce::Array<double>(); }
    juce::Array<int> getAvailableBufferSizes() override { return usable() ? inner->getAvailableBufferSizes() : juce::Array<int>(); }
    int getDefaultBufferSize() override { return usable() ? inner->getDefaultBufferSize() : 512; }

    juce::String open (const juce::BigInteger& ins, const juce::BigInteger& outs, double rate, int size) override
    {
        if (! usable() || isHung (getTypeName(), getName(), true))
            return error = hungError (getName());
        struct State
        {
            std::shared_ptr<juce::AudioIODevice> device;
            juce::String result;
            juce::WaitableEvent done;
        };
        auto state = std::make_shared<State>();
        state->device = inner;
        if (runBounded<State> (state, [ins, outs, rate, size] (State& s) { s.result = s.device->open (ins, outs, rate, size); }))
            return error = state->result;
        // the driver did not answer: the worker keeps the device; nothing here touches it again
        abandoned = true;
        abandonedInType->store (true);
        inner.reset();
        markHung (getTypeName(), getName());
        return error = hungError (getName());
    }
    void close() override
    {
        if (usable())
            inner->close();
    }
    bool isOpen() override { return usable() && inner->isOpen(); }
    void start (juce::AudioIODeviceCallback* callback) override
    {
        if (usable())
            inner->start (callback);
    }
    void stop() override
    {
        if (usable())
            inner->stop();
    }
    bool isPlaying() override { return usable() && inner->isPlaying(); }
    juce::String getLastError() override { return error.isNotEmpty() ? error : usable() ? inner->getLastError() : juce::String(); }
    int getCurrentBufferSizeSamples() override { return usable() ? inner->getCurrentBufferSizeSamples() : 0; }
    double getCurrentSampleRate() override { return usable() ? inner->getCurrentSampleRate() : 0.0; }
    int getCurrentBitDepth() override { return usable() ? inner->getCurrentBitDepth() : 0; }
    juce::BigInteger getActiveOutputChannels() const override { return usable() ? inner->getActiveOutputChannels() : juce::BigInteger(); }
    juce::BigInteger getActiveInputChannels() const override { return usable() ? inner->getActiveInputChannels() : juce::BigInteger(); }
    int getOutputLatencyInSamples() override { return usable() ? inner->getOutputLatencyInSamples() : 0; }
    int getInputLatencyInSamples() override { return usable() ? inner->getInputLatencyInSamples() : 0; }
    juce::AudioWorkgroup getWorkgroup() const override { return usable() ? inner->getWorkgroup() : juce::AudioWorkgroup(); }
    bool hasControlPanel() const override { return usable() && inner->hasControlPanel(); }
    bool showControlPanel() override { return usable() && inner->showControlPanel(); }
    bool setAudioPreprocessingEnabled (bool on) override { return usable() && inner->setAudioPreprocessingEnabled (on); }
    int getXRunCount() const noexcept override { return usable() ? inner->getXRunCount() : -1; }

    juce::AudioIODevice* innerDevice() const noexcept { return inner.get(); }

private:
    bool usable() const noexcept { return ! abandoned && inner != nullptr; }
    std::shared_ptr<juce::AudioIODevice> inner;
    std::shared_ptr<std::atomic<bool>> abandonedInType;
    bool abandoned = false;
    juce::String error;
};

class GuardedType final : public juce::AudioIODeviceType,
                          private juce::AudioIODeviceType::Listener
{
public:
    explicit GuardedType (std::unique_ptr<juce::AudioIODeviceType> t)
        : juce::AudioIODeviceType (t->getTypeName()), type (std::move (t))
    {
        type->addListener (this);
    }
    ~GuardedType() override
    {
        type->removeListener (this);
        if (abandoned->load()) // (a worker may still be inside this type's driver: the type stays, never deleted)
            (void) type.release();
    }

    void scanForDevices() override { type->scanForDevices(); }
    juce::StringArray getDeviceNames (bool wantInputNames) const override { return type->getDeviceNames (wantInputNames); }
    int getDefaultDeviceIndex (bool forInput) const override { return type->getDefaultDeviceIndex (forInput); }
    int getIndexOfDevice (juce::AudioIODevice* d, bool asInput) const override
    {
        if (auto* g = dynamic_cast<GuardedDevice*> (d))
        {
            if (auto* in = g->innerDevice())
                return type->getIndexOfDevice (in, asInput);
            return getDeviceNames (asInput).indexOf (d->getName());
        }
        return type->getIndexOfDevice (d, asInput);
    }
    bool hasSeparateInputsAndOutputs() const override { return type->hasSeparateInputsAndOutputs(); }

    juce::AudioIODevice* createDevice (const juce::String& outputName, const juce::String& inputName) override
    {
        const auto name = outputName.isNotEmpty() ? outputName : inputName;
        if (isHung (getTypeName(), name)) // (its open fails at once with the reason)
            return new GuardedDevice ({}, name, getTypeName(), abandoned);
        struct State
        {
            juce::AudioIODeviceType* type = nullptr;
            juce::String out, in;
            std::unique_ptr<juce::AudioIODevice> device;
            juce::WaitableEvent done;
        };
        auto state = std::make_shared<State>();
        state->type = type.get();
        state->out = outputName;
        state->in = inputName;
        if (! runBounded<State> (state, [] (State& s) { s.device.reset (s.type->createDevice (s.out, s.in)); }))
        {
            abandoned->store (true);
            markHung (getTypeName(), name);
            return new GuardedDevice ({}, name, getTypeName(), abandoned);
        }
        if (state->device == nullptr)
            return nullptr;
        const auto deviceName = state->device->getName();
        return new GuardedDevice (std::shared_ptr<juce::AudioIODevice> (std::move (state->device)), deviceName, getTypeName(), abandoned);
    }

private:
    void audioDeviceListChanged() override { callDeviceChangeListeners(); }

    std::unique_ptr<juce::AudioIODeviceType> type;
    std::shared_ptr<std::atomic<bool>> abandoned = std::make_shared<std::atomic<bool>> (false);
};

// --- the test type --------------------------------------------------------------------------------------------------
class AnswersDevice final : public juce::AudioIODevice, private juce::Thread
{
public:
    AnswersDevice() : juce::AudioIODevice ("Answers", hangTestTypeName), juce::Thread ("hang test device") {}
    ~AnswersDevice() override { close(); }
    juce::StringArray getOutputChannelNames() override { return { "Out 1", "Out 2" }; }
    juce::StringArray getInputChannelNames() override { return {}; }
    juce::Array<double> getAvailableSampleRates() override { return { 48000.0 }; }
    juce::Array<int> getAvailableBufferSizes() override { return { 512 }; }
    int getDefaultBufferSize() override { return 512; }
    juce::String open (const juce::BigInteger&, const juce::BigInteger& outs, double, int) override
    {
        close();
        active = outs;
        active.setRange (2, juce::jmax (0, active.getHighestBit() - 1), false);
        data.setSize (2, 512);
        opened = true;
        startThread();
        return {};
    }
    void close() override
    {
        stop();
        stopThread (5000);
        opened = false;
    }
    bool isOpen() override { return opened; }
    void start (juce::AudioIODeviceCallback* cb) override
    {
        if (! opened || cb == nullptr)
            return;
        cb->audioDeviceAboutToStart (this);
        const juce::ScopedLock sl (lock);
        callback = cb;
    }
    void stop() override
    {
        juce::AudioIODeviceCallback* old = nullptr;
        {
            const juce::ScopedLock sl (lock);
            old = std::exchange (callback, nullptr);
        }
        if (old != nullptr)
            old->audioDeviceStopped();
    }
    bool isPlaying() override { return callback != nullptr; }
    juce::String getLastError() override { return {}; }
    int getCurrentBufferSizeSamples() override { return 512; }
    double getCurrentSampleRate() override { return 48000.0; }
    int getCurrentBitDepth() override { return 32; }
    juce::BigInteger getActiveOutputChannels() const override { return active; }
    juce::BigInteger getActiveInputChannels() const override { return {}; }
    int getOutputLatencyInSamples() override { return 512; }
    int getInputLatencyInSamples() override { return 0; }

private:
    void run() override
    {
        auto next = juce::Time::getMillisecondCounterHiRes();
        while (! threadShouldExit())
        {
            {
                const juce::ScopedLock sl (lock);
                if (callback != nullptr)
                    callback->audioDeviceIOCallbackWithContext (nullptr, 0, data.getArrayOfWritePointers(), 2, 512, {});
            }
            next += 512.0 / 48.0;
            while (juce::Time::getMillisecondCounterHiRes() < next && ! threadShouldExit())
                juce::Thread::sleep (1);
        }
    }
    juce::CriticalSection lock;
    juce::AudioIODeviceCallback* callback = nullptr;
    juce::AudioBuffer<float> data;
    juce::BigInteger active;
    bool opened = false;
};

// its open never returns (a driver stuck inside its own open)
class HangsDevice final : public juce::AudioIODevice
{
public:
    HangsDevice() : juce::AudioIODevice ("Hangs on open", hangTestTypeName) {}
    juce::StringArray getOutputChannelNames() override { return { "Out 1", "Out 2" }; }
    juce::StringArray getInputChannelNames() override { return {}; }
    juce::Array<double> getAvailableSampleRates() override { return { 48000.0 }; }
    juce::Array<int> getAvailableBufferSizes() override { return { 512 }; }
    int getDefaultBufferSize() override { return 512; }
    juce::String open (const juce::BigInteger&, const juce::BigInteger&, double, int) override
    {
        for (;;)
            std::this_thread::sleep_for (std::chrono::milliseconds (100));
    }
    void close() override {}
    bool isOpen() override { return false; }
    void start (juce::AudioIODeviceCallback*) override {}
    void stop() override {}
    bool isPlaying() override { return false; }
    juce::String getLastError() override { return {}; }
    int getCurrentBufferSizeSamples() override { return 512; }
    double getCurrentSampleRate() override { return 48000.0; }
    int getCurrentBitDepth() override { return 32; }
    juce::BigInteger getActiveOutputChannels() const override { return {}; }
    juce::BigInteger getActiveInputChannels() const override { return {}; }
    int getOutputLatencyInSamples() override { return 0; }
    int getInputLatencyInSamples() override { return 0; }
};

class HangTestType final : public juce::AudioIODeviceType
{
public:
    HangTestType() : juce::AudioIODeviceType (hangTestTypeName) {}
    void scanForDevices() override {}
    juce::StringArray getDeviceNames (bool input) const override { return input ? juce::StringArray() : juce::StringArray ("Answers", "Hangs on open"); }
    int getDefaultDeviceIndex (bool input) const override { return input ? -1 : 0; }
    int getIndexOfDevice (juce::AudioIODevice* d, bool input) const override { return d != nullptr && ! input ? getDeviceNames (false).indexOf (d->getName()) : -1; }
    bool hasSeparateInputsAndOutputs() const override { return false; }
    juce::AudioIODevice* createDevice (const juce::String& out, const juce::String&) override
    {
        if (out == "Answers")
            return new AnswersDevice();
        if (out == "Hangs on open")
            return new HangsDevice();
        return nullptr;
    }
};
} // namespace

std::unique_ptr<juce::AudioIODeviceType> wrap (std::unique_ptr<juce::AudioIODeviceType> inner)
{
    if (inner == nullptr || juce::SystemStats::getEnvironmentVariable ("LYNX_DEVICE_GUARD", {}) == "0")
        return inner;
    return std::make_unique<GuardedType> (std::move (inner));
}

std::unique_ptr<juce::AudioIODeviceType> makeHangTestType (bool forCheck)
{
    if (! forCheck && (! platform::testMode() || juce::SystemStats::getEnvironmentVariable ("LYNX_TEST_DEVICE_HANG", {}) != "1"))
        return nullptr;
    return std::make_unique<HangTestType>();
}

namespace
{
std::atomic<int> checkTimeout { 0 };
}

void setTimeoutForChecks (int ms)
{
    checkTimeout.store (ms);
}

int timeoutMs()
{
    if (const auto c = checkTimeout.load(); c > 0)
        return c;
    const auto v = juce::SystemStats::getEnvironmentVariable ("LYNX_DEVICE_OPEN_TIMEOUT_MS", {}).getIntValue();
    return v >= 200 ? v : 10000;
}

void markHungForPrewarm (const juce::String& typeName, const juce::String& deviceName)
{
    markHung (typeName, deviceName);
}

juce::String lastHung()
{
    auto& r = registry();
    const std::lock_guard<std::mutex> l (r.lock);
    return r.last;
}

int hungCount()
{
    auto& r = registry();
    const std::lock_guard<std::mutex> l (r.lock);
    return r.count;
}

bool isHung (const juce::String& typeName, const juce::String& deviceName, bool consume)
{
    auto& r = registry();
    const std::lock_guard<std::mutex> l (r.lock);
    if (consume ? r.again.erase ({ typeName, deviceName }) > 0 : r.again.count ({ typeName, deviceName }) > 0) // (one more attempt asked for)
        return false;
    return r.hung.count ({ typeName, deviceName }) > 0;
}

void allowAgain (const juce::String& typeName, const juce::String& deviceName)
{
    auto& r = registry();
    const std::lock_guard<std::mutex> l (r.lock);
    r.again.insert ({ typeName, deviceName });
}

juce::String hungError (const juce::String& deviceName)
{
    return "The driver of " + deviceName + " did not answer within " + juce::String (timeoutMs() / 1000.0, 1) + " s";
}

bool waitAnsweringSentMessages (juce::WaitableEvent& done, int ms)
{
   #if JUCE_WINDOWS
    const auto end = juce::Time::getMillisecondCounterHiRes() + ms;
    while (juce::Time::getMillisecondCounterHiRes() < end)
    {
        if (done.wait (5))
            return true;
        MSG m;
        PeekMessageW (&m, nullptr, 0, 0, PM_NOREMOVE | PM_QS_SENDMESSAGE); // (sent messages are answered inside)
    }
    return done.wait (0);
   #else
    return done.wait (ms);
   #endif
}
} // namespace lynx::deviceguard
