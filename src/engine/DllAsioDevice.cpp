#include "engine/DllAsioDevice.h"
#include <juce_gui_basics/juce_gui_basics.h>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <objbase.h>
 #include "asiosys.h"
 #include "asio.h"
 #include "iasiodrv.h"
 #include <atomic>
 #include <cmath>
 #include <utility>
 #include <thread>
 #include <vector>

namespace lynx
{
namespace
{
// LYNX ASIO's class id (asio/LynxAsio.cpp)
constexpr const wchar_t* lynxAsioClsid = L"{E987C593-898E-4557-BFE3-4288B0228718}";

class DllAsioDevice;
std::atomic<DllAsioDevice*> activeDevice { nullptr }; // ASIO callbacks carry no user pointer: one open device at a time

class DllAsioDevice final : public juce::AudioIODevice
{
public:
    DllAsioDevice (const juce::String& name, const juce::String& typeName, const juce::String& dllPath, const juce::String& clsidText)
        : juce::AudioIODevice (name, typeName)
    {
        module = LoadLibraryW (dllPath.toWideCharPointer());
        if (module == nullptr)
        {
            lastError = "could not load " + dllPath;
            return;
        }
        using GetClassObject = HRESULT (WINAPI*) (REFCLSID, REFIID, void**);
        auto get = (GetClassObject) (void*) GetProcAddress (module, "DllGetClassObject");
        CLSID clsid {};
        if (get == nullptr || FAILED (CLSIDFromString ((LPCOLESTR) (clsidText.isEmpty() ? juce::String (lynxAsioClsid) : clsidText).toWideCharPointer(), &clsid)))
        {
            lastError = "not an ASIO driver DLL";
            return;
        }
        IClassFactory* factory = nullptr;
        if (FAILED (get (clsid, IID_IClassFactory, (void**) &factory)))
        {
            lastError = "the DLL does not offer that class";
            return;
        }
        void* object = nullptr;
        const auto hr = factory->CreateInstance (nullptr, clsid, &object); // ASIO: the class id doubles as the interface id
        factory->Release();
        if (FAILED (hr) || object == nullptr)
        {
            lastError = "the driver could not be created";
            return;
        }
        driver = (IASIO*) object;
        if (driver->init (juce::TopLevelWindow::getNumTopLevelWindows() > 0 ? juce::TopLevelWindow::getTopLevelWindow (0)->getWindowHandle() : nullptr) == ASIOFalse)
        {
            char text[128] {};
            driver->getErrorMessage (text);
            lastError = "driver init failed: " + juce::String (text);
            driver->Release();
            driver = nullptr;
            return;
        }
        readCapabilities();
    }

    ~DllAsioDevice() override
    {
        close();
        if (driver != nullptr)
            driver->Release();
        if (module != nullptr)
            FreeLibrary (module);
    }

    bool isValid() const { return driver != nullptr; }

    juce::StringArray getOutputChannelNames() override { return outputNames; }
    juce::StringArray getInputChannelNames() override { return inputNames; }
    juce::Array<double> getAvailableSampleRates() override { return rates; }
    juce::Array<int> getAvailableBufferSizes() override { return bufferSizes; }
    int getDefaultBufferSize() override { return preferredSize; }

    juce::String open (const juce::BigInteger& inputs, const juce::BigInteger& outputs, double rate, int bufferSize) override
    {
        close();
        if (driver == nullptr)
            return lastError;
        lastError = {};
        if (rate > 0 && driver->setSampleRate (rate) != ASE_OK)
            lastError = "sample rate " + juce::String (rate) + " refused";
        ASIOSampleRate actual = 0;
        driver->getSampleRate (&actual);
        currentRate = actual;
        currentSize = bufferSize > 0 ? bufferSize : preferredSize;

        activeIns = inputs;
        activeIns.setRange (inputNames.size(), juce::jmax (0, activeIns.getHighestBit() + 1 - inputNames.size()), false);
        activeOuts = outputs;
        activeOuts.setRange (outputNames.size(), juce::jmax (0, activeOuts.getHighestBit() + 1 - outputNames.size()), false);
        infos.clear();
        for (int i = 0; i < inputNames.size(); ++i)
            if (activeIns[i])
                infos.push_back ({ ASIOTrue, i, { nullptr, nullptr } });
        for (int i = 0; i < outputNames.size(); ++i)
            if (activeOuts[i])
                infos.push_back ({ ASIOFalse, i, { nullptr, nullptr } });
        if (infos.empty())
            return lastError = "no channels";

        static ASIOCallbacks callbacks { bufferSwitch, sampleRateChanged, asioMessage, bufferSwitchTimeInfo };
        activeDevice = this;
        if (driver->createBuffers (infos.data(), (long) infos.size(), currentSize, &callbacks) != ASE_OK)
        {
            char text[128] {};
            driver->getErrorMessage (text);
            activeDevice = nullptr;
            return lastError = "createBuffers failed: " + juce::String (text);
        }
        types.clear();
        for (auto& info : infos)
        {
            ASIOChannelInfo ci {};
            ci.channel = info.channelNum;
            ci.isInput = info.isInput;
            driver->getChannelInfo (&ci);
            types.push_back (ci.type);
        }
        long inLatency = 0, outLatency = 0;
        driver->getLatencies (&inLatency, &outLatency);
        inputLatency = (int) inLatency;
        outputLatency = (int) outLatency;
        floatIns.setSize (juce::jmax (1, activeIns.countNumberOfSetBits()), currentSize);
        floatOuts.setSize (juce::jmax (1, activeOuts.countNumberOfSetBits()), currentSize);
        opened = true;
        return {};
    }

    void close() override
    {
        stop();
        if (opened)
        {
            driver->disposeBuffers();
            opened = false;
        }
        if (activeDevice == this)
            activeDevice = nullptr;
    }

    bool isOpen() override { return opened; }

    void start (juce::AudioIODeviceCallback* cb) override
    {
        if (! opened || cb == nullptr)
            return;
        cb->audioDeviceAboutToStart (this);
        {
            const juce::ScopedLock sl (lock);
            callback = cb;
        }
        playing = driver->start() == ASE_OK;
        if (! playing)
            lastError = "start failed";
    }

    void stop() override
    {
        if (playing)
            driver->stop();
        playing = false;
        juce::AudioIODeviceCallback* old = nullptr;
        {
            const juce::ScopedLock sl (lock);
            old = std::exchange (callback, nullptr);
        }
        if (old != nullptr)
            old->audioDeviceStopped();
    }

    bool isPlaying() override { return playing; }
    juce::String getLastError() override { return lastError; }
    int getCurrentBufferSizeSamples() override { return currentSize; }
    double getCurrentSampleRate() override { return currentRate; }
    int getCurrentBitDepth() override { return 32; }
    juce::BigInteger getActiveOutputChannels() const override { return activeOuts; }
    juce::BigInteger getActiveInputChannels() const override { return activeIns; }
    int getOutputLatencyInSamples() override { return outputLatency; }
    int getInputLatencyInSamples() override { return inputLatency; }
    bool hasControlPanel() const override { return true; }
    bool showControlPanel() override
    {
        driver->controlPanel();
        return false;
    }

private:
    void readCapabilities()
    {
        long ins = 0, outs = 0;
        driver->getChannels (&ins, &outs);
        for (long i = 0; i < ins + outs; ++i)
        {
            ASIOChannelInfo ci {};
            ci.isInput = i < ins ? ASIOTrue : ASIOFalse;
            ci.channel = i < ins ? i : i - ins;
            driver->getChannelInfo (&ci);
            (i < ins ? inputNames : outputNames).add (juce::String (ci.name).trim());
        }
        for (double r : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
            if (driver->canSampleRate (r) == ASE_OK)
                rates.add (r);
        long mn = 0, mx = 0, pref = 0, gran = 0;
        driver->getBufferSize (&mn, &mx, &pref, &gran);
        preferredSize = (int) pref;
        for (int s = 16; s <= juce::jmin (4096L, mx); s *= 2)
            if (s >= mn)
                bufferSizes.add (s);
        if (gran > 0)
            for (long s = mn; s <= juce::jmin (2048L, mx); s += juce::jmax (16L, gran))
                bufferSizes.addIfNotAlreadyThere ((int) s);
        bufferSizes.addIfNotAlreadyThere ((int) pref);
        bufferSizes.addIfNotAlreadyThere ((int) mn);
        bufferSizes.sort();
    }

    static float toFloat (const void* data, int i, ASIOSampleType type)
    {
        switch (type)
        {
            case ASIOSTFloat32LSB: return ((const float*) data)[i];
            case ASIOSTInt32LSB:   return (float) (((const int*) data)[i] / 2147483648.0);
            case ASIOSTInt16LSB:   return ((const short*) data)[i] / 32768.0f;
            case ASIOSTInt24LSB:
            {
                auto* b = (const unsigned char*) data + 3 * i;
                return (float) ((int) ((unsigned) b[0] << 8 | (unsigned) b[1] << 16 | (unsigned) b[2] << 24) >> 8) / 8388608.0f;
            }
            default: return 0.0f;
        }
    }

    static void fromFloat (void* data, int i, float v, ASIOSampleType type)
    {
        v = juce::jlimit (-1.0f, 1.0f, v);
        switch (type)
        {
            case ASIOSTFloat32LSB: ((float*) data)[i] = v; break;
            case ASIOSTInt32LSB:   ((int*) data)[i] = (int) std::lrint (v * 2147483647.0); break;
            case ASIOSTInt16LSB:   ((short*) data)[i] = (short) std::lrint (v * 32767.0f); break;
            case ASIOSTInt24LSB:
            {
                const auto s = (int) std::lrint (v * 8388607.0f);
                auto* b = (unsigned char*) data + 3 * i;
                b[0] = (unsigned char) (s & 0xff);
                b[1] = (unsigned char) ((s >> 8) & 0xff);
                b[2] = (unsigned char) ((s >> 16) & 0xff);
                break;
            }
            default: break;
        }
    }

    void process (long index)
    {
        const juce::ScopedLock sl (lock);
        const int n = currentSize;
        int in = 0, out = 0;
        for (size_t c = 0; c < infos.size(); ++c)
            if (infos[c].isInput)
            {
                auto* dst = floatIns.getWritePointer (in++);
                for (int i = 0; i < n; ++i)
                    dst[i] = toFloat (infos[c].buffers[index], i, types[c]);
            }
        const int numIns = activeIns.countNumberOfSetBits(), numOuts = activeOuts.countNumberOfSetBits();
        if (callback != nullptr)
        {
            const juce::AudioIODeviceCallbackContext context;
            callback->audioDeviceIOCallbackWithContext (floatIns.getArrayOfReadPointers(), numIns, floatOuts.getArrayOfWritePointers(), numOuts, n, context);
        }
        else
            floatOuts.clear();
        for (size_t c = 0; c < infos.size(); ++c)
            if (! infos[c].isInput)
            {
                const auto* src = floatOuts.getReadPointer (out++);
                for (int i = 0; i < n; ++i)
                    fromFloat (infos[c].buffers[index], i, src[i], types[c]);
            }
        driver->outputReady();
    }

    void requestReset()
    {
        juce::MessageManager::callAsync ([safe = juce::WeakReference<DllAsioDevice> (this)]
        {
            if (auto* d = safe.get(); d != nullptr && d->opened)
            {
                auto* cb = d->callback;
                const auto ins = d->activeIns, outs = d->activeOuts;
                const auto rate = d->currentRate;
                const auto size = d->currentSize;
                d->callback = nullptr;
                d->stop();
                d->open (ins, outs, rate, size);
                if (cb != nullptr)
                    d->start (cb);
            }
        });
    }

    static void bufferSwitch (long index, ASIOBool)
    {
        if (auto* d = activeDevice.load())
            d->process (index);
    }
    static ASIOTime* bufferSwitchTimeInfo (ASIOTime*, long index, ASIOBool)
    {
        bufferSwitch (index, ASIOTrue);
        return nullptr;
    }
    static void sampleRateChanged (ASIOSampleRate) {}
    static long asioMessage (long selector, long value, void*, double*)
    {
        switch (selector)
        {
            case kAsioSelectorSupported:
                return value == kAsioResetRequest || value == kAsioEngineVersion || value == kAsioResyncRequest || value == kAsioLatenciesChanged ? 1 : 0;
            case kAsioEngineVersion:
                return 2;
            case kAsioResetRequest:
                if (auto* d = activeDevice.load())
                    d->requestReset();
                return 1;
            case kAsioLatenciesChanged: // e.g. LYNX ASIO raised its input FIFO after late packets
                if (auto* d = activeDevice.load(); d != nullptr && d->driver != nullptr)
                {
                    long in = 0, out = 0;
                    if (d->driver->getLatencies (&in, &out) == ASE_OK)
                    {
                        d->inputLatency = (int) in;
                        d->outputLatency = (int) out;
                    }
                }
                return 1;
            case kAsioResyncRequest:
                return 1;
            default:
                return 0;
        }
    }

    HMODULE module = nullptr;
    IASIO* driver = nullptr;
    juce::String lastError;
    juce::StringArray inputNames, outputNames;
    juce::Array<double> rates;
    juce::Array<int> bufferSizes;
    int preferredSize = 512, currentSize = 512;
    std::atomic<int> inputLatency { 0 }, outputLatency { 0 };
    double currentRate = 48000.0;
    juce::BigInteger activeIns, activeOuts;
    std::vector<ASIOBufferInfo> infos;
    std::vector<ASIOSampleType> types;
    juce::AudioBuffer<float> floatIns, floatOuts;
    juce::CriticalSection lock;
    juce::AudioIODeviceCallback* callback = nullptr;
    bool opened = false, playing = false;

    JUCE_DECLARE_WEAK_REFERENCEABLE (DllAsioDevice)
};

class DllAsioDeviceType final : public juce::AudioIODeviceType
{
public:
    DllAsioDeviceType (const juce::String& s, const juce::String& typeName, const juce::String& deviceName)
        : juce::AudioIODeviceType (typeName.isNotEmpty() ? typeName : juce::String (dllAsioTypeName))
    {
        path = s.upToFirstOccurrenceOf ("|", false, false).unquoted();
        clsid = s.fromFirstOccurrenceOf ("|", false, false).trim();
        name = deviceName.isNotEmpty() ? deviceName : juce::File (path).getFileNameWithoutExtension();
    }
    void scanForDevices() override {}
    juce::StringArray getDeviceNames (bool) const override { return { name }; }
    int getDefaultDeviceIndex (bool) const override { return 0; }
    int getIndexOfDevice (juce::AudioIODevice* d, bool) const override { return d != nullptr && d->getName() == name ? 0 : -1; }
    bool hasSeparateInputsAndOutputs() const override { return false; }
    juce::AudioIODevice* createDevice (const juce::String& out, const juce::String& in) override
    {
        if ((out.isNotEmpty() && out != name) || (in.isNotEmpty() && in != name))
            return nullptr;
        auto device = std::make_unique<DllAsioDevice> (name, getTypeName(), path, clsid);
        return device->isValid() ? device.release() : nullptr;
    }

private:
    juce::String path, clsid, name;
};
} // namespace

std::unique_ptr<juce::AudioIODeviceType> makeDllAsioDeviceType (const juce::String& spec, const juce::String& typeName, const juce::String& deviceName)
{
    return std::make_unique<DllAsioDeviceType> (spec, typeName, deviceName);
}

std::unique_ptr<juce::AudioIODeviceType> makeBuiltInLynxAsioType()
{
    const auto dll = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getSiblingFile ("lynx-asio.dll");
    if (! dll.existsAsFile() || juce::WindowsRegistry::keyExists ("HKEY_LOCAL_MACHINE\\SOFTWARE\\ASIO\\LYNX ASIO"))
        return nullptr;
    return makeDllAsioDeviceType (dll.getFullPathName(), lynxAsioTypeName, lynxAsioTypeName);
}

namespace
{
// a REG_SZ / REG_EXPAND_SZ value of an open key ({} when missing)
std::wstring registryText (HKEY key, const wchar_t* value)
{
    wchar_t buffer[1024] = {};
    DWORD type = 0, size = sizeof (buffer) - sizeof (wchar_t);
    if (RegQueryValueExW (key, value, nullptr, &type, reinterpret_cast<LPBYTE> (buffer), &size) != ERROR_SUCCESS
        || (type != REG_SZ && type != REG_EXPAND_SZ))
        return {};
    std::wstring text (buffer);
    if (type == REG_EXPAND_SZ)
    {
        wchar_t expanded[1024] = {};
        if (const auto n = ExpandEnvironmentStringsW (text.c_str(), expanded, 1024); n > 0 && n <= 1024)
            text = expanded;
    }
    return text;
}

// The DLL of the ASIO driver JUCE lists as `deviceName` (its key under HKLM\SOFTWARE\ASIO, named by its
// "description" or else by the key's name, as JUCE's ASIO type does), from its class's InprocServer32.
std::wstring asioDriverDll (const std::wstring& deviceName)
{
    HKEY asio = nullptr;
    if (RegOpenKeyExW (HKEY_LOCAL_MACHINE, L"SOFTWARE\\ASIO", 0, KEY_READ, &asio) != ERROR_SUCCESS)
        return {};
    std::wstring clsid;
    wchar_t name[256] = {};
    for (DWORD i = 0; clsid.empty() && RegEnumKeyW (asio, i, name, 256) == ERROR_SUCCESS; ++i)
    {
        HKEY driver = nullptr;
        if (RegOpenKeyExW (asio, name, 0, KEY_READ, &driver) != ERROR_SUCCESS)
            continue;
        auto shown = registryText (driver, L"description");
        if (shown.empty())
            shown = name;
        if (shown == deviceName)
            clsid = registryText (driver, L"clsid");
        RegCloseKey (driver);
    }
    RegCloseKey (asio);
    if (clsid.empty())
        return {};
    HKEY server = nullptr;
    if (RegOpenKeyExW (HKEY_CLASSES_ROOT, (L"CLSID\\" + clsid + L"\\InprocServer32").c_str(), 0, KEY_READ, &server) != ERROR_SUCCESS)
        return {};
    auto dll = registryText (server, nullptr);
    RegCloseKey (server);
    return dll;
}
} // namespace

void preloadAsioDriver (const juce::File& savedSetup)
{
    std::thread ([savedSetup] {
        const auto xml = juce::XmlDocument::parse (savedSetup);
        if (xml == nullptr || xml->getStringAttribute ("deviceType") != "ASIO")
            return;
        const auto device = xml->getStringAttribute ("audioOutputDeviceName").isNotEmpty() ? xml->getStringAttribute ("audioOutputDeviceName")
                                                                                         : xml->getStringAttribute ("audioInputDeviceName");
        if (device.isEmpty())
            return;
        const auto dll = asioDriverDll (device.toWideCharPointer());
        if (dll.empty())
            return;
        // kept loaded for the session (COM loads the same module when the driver is created; never unloaded here)
        LoadLibraryExW (dll.c_str(), nullptr, 0);
    }).detach();
}
} // namespace lynx

#else

namespace lynx
{
std::unique_ptr<juce::AudioIODeviceType> makeDllAsioDeviceType (const juce::String&, const juce::String&, const juce::String&) { return nullptr; }
std::unique_ptr<juce::AudioIODeviceType> makeBuiltInLynxAsioType() { return nullptr; }
void preloadAsioDriver (const juce::File&) {}
} // namespace lynx

#endif
