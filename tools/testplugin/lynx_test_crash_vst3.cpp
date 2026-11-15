// A VST3 effect that crashes on purpose (round 190dt: LYNX's plugin crash guard on Linux / macOS, and Windows' SEH
// guard through JUCE's VST3 host). Main stereo in -> out at 0.5 gain, a 4-byte state ("LXV1"); no parameters, no
// editor. Built only against the VST3 SDK's pluginterfaces (no SDK helper classes), like lynx_test_sidechain_vst3.cpp.
// Environment variable LYNX_TEST_CRASH:
//   "process" - a null-pointer write (SIGSEGV / access violation) in the Nth process() call with audio,
//               N = LYNX_TEST_CRASH_AFTER (default 20); counted per loaded module, so an instance made again later
//               (LYNX's Reload) does not crash again
//   "memcpy"  - the same as a memcpy from a null pointer: the fault is inside the C library (what nih-plug did, #155)
//   "state"   - a null-pointer write when a saved state is loaded into it (IComponent::setState)
//   "thread"  - (Linux / macOS; round 190gg) in the Nth process() call it starts a thread of its own that writes to a
//               null pointer: a fault outside every call the host makes (LYNX --crashguardtest ... thread)
//
//   V=<JUCE>/modules/juce_audio_processors/format_types/VST3_SDK
//   Linux:   B="LYNX Test Crash.vst3/Contents/x86_64-linux"; mkdir -p "$B"
//            c++ -shared -fPIC -O2 -std=c++17 -I$V tools/testplugin/lynx_test_crash_vst3.cpp
//                $V/pluginterfaces/base/funknown.cpp -o "$B/LYNX Test Crash.so"                 (one line)
//   Windows: zig c++ -shared -O2 -std=c++17 -target x86_64-windows-gnu -I$V tools/testplugin/lynx_test_crash_vst3.cpp
//                $V/pluginterfaces/base/funknown.cpp -lole32 -o "LYNX Test Crash.vst3"          (one line)
//   then:    LYNX_TEST_CRASH=process LYNX --crashguardtest "LYNX Test Crash.vst3" report.txt
#define INIT_CLASS_IID
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/gui/iplugview.h"

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#if ! defined(_WIN32)
 #include <pthread.h>
#endif

#if defined(_WIN32)
 #include <windows.h>
 #define LYNX_TEST_EXPORT __declspec (dllexport)
#else
 #define LYNX_TEST_EXPORT __attribute__ ((visibility ("default")))
#endif

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace
{
const FUID effectUid (0x4C594E58, 0x43524153, 0x48313930, 0x44545633); // "LYNXCRASH190DTV3"

// (read at every use: the host may change them between calls; on Windows from the process, not this DLL's C runtime)
const char* environment (const char* name, char (&buffer)[32])
{
#if defined(_WIN32)
    return GetEnvironmentVariableA (name, buffer, sizeof (buffer)) > 0 ? buffer : nullptr;
#else
    (void) buffer;
    return std::getenv (name);
#endif
}

bool crashMode (const char* what)
{
    char buffer[32] {};
    const char* value = environment ("LYNX_TEST_CRASH", buffer);
    return value != nullptr && std::strcmp (value, what) == 0;
}

int crashAfter()
{
    char buffer[32] {};
    const char* value = environment ("LYNX_TEST_CRASH_AFTER", buffer);
    const auto n = value != nullptr ? std::atoi (value) : 0;
    return n > 0 ? n : 20;
}

volatile int sink = 0;
std::atomic<int> processCalls { 0 }; // (per module)

#if defined(__GNUC__) || defined(__clang__)
__attribute__ ((noinline))
#endif
void nullWrite()
{
    volatile std::uintptr_t address = 0; // (opaque: a write to a constant null is undefined, and g++ drops the whole path)
    *reinterpret_cast<volatile int*> (address) = 1;
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__ ((noinline))
#endif
void nullCopy()
{
    char to[64];
    const char* volatile from = nullptr;
    volatile std::size_t size = sizeof (to); // (a real call into the C library)
    std::memcpy (to, (const char*) from, size);
    sink = to[0];
}

#if ! defined(_WIN32)
void* faultingThread (void*)
{
    nullWrite();
    return nullptr;
}

void startFaultingThread()
{
    pthread_t thread;
    if (pthread_create (&thread, nullptr, faultingThread, nullptr) == 0)
        pthread_detach (thread);
}
#endif

void copyString (String128 to, const char* from)
{
    int i = 0;
    for (; from[i] != 0 && i < 127; ++i)
        to[i] = (char16) from[i];
    to[i] = 0;
}

class TestCrash final : public IComponent, public IAudioProcessor, public IEditController
{
public:
    // FUnknown
    tresult PLUGIN_API queryInterface (const TUID iid, void** obj) override
    {
        if (FUnknownPrivate::iidEqual (iid, FUnknown::iid) || FUnknownPrivate::iidEqual (iid, IPluginBase::iid)
            || FUnknownPrivate::iidEqual (iid, IComponent::iid))
        {
            addRef();
            *obj = static_cast<IComponent*> (this);
            return kResultOk;
        }
        if (FUnknownPrivate::iidEqual (iid, IAudioProcessor::iid))
        {
            addRef();
            *obj = static_cast<IAudioProcessor*> (this);
            return kResultOk;
        }
        if (FUnknownPrivate::iidEqual (iid, IEditController::iid))
        {
            addRef();
            *obj = static_cast<IEditController*> (this);
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return ++refs; }
    uint32 PLUGIN_API release() override
    {
        const auto r = --refs;
        if (r == 0)
            delete this;
        return r;
    }

    // IPluginBase
    tresult PLUGIN_API initialize (FUnknown*) override { return kResultOk; }
    tresult PLUGIN_API terminate() override { return kResultOk; }

    // IComponent: one stereo main input and output
    tresult PLUGIN_API getControllerClassId (TUID) override { return kNotImplemented; } // single component
    tresult PLUGIN_API setIoMode (IoMode) override { return kResultOk; }
    int32 PLUGIN_API getBusCount (MediaType type, BusDirection) override { return type == kAudio ? 1 : 0; }
    tresult PLUGIN_API getBusInfo (MediaType type, BusDirection dir, int32 index, BusInfo& bus) override
    {
        if (type != kAudio || index != 0)
            return kInvalidArgument;
        std::memset (&bus, 0, sizeof (bus));
        bus.mediaType = type;
        bus.direction = dir;
        bus.channelCount = 2;
        bus.busType = kMain;
        bus.flags = BusInfo::kDefaultActive;
        copyString (bus.name, dir == kOutput ? "Output" : "Input");
        return kResultOk;
    }
    tresult PLUGIN_API getRoutingInfo (RoutingInfo&, RoutingInfo&) override { return kNotImplemented; }
    tresult PLUGIN_API activateBus (MediaType, BusDirection, int32, TBool) override { return kResultOk; }
    tresult PLUGIN_API setActive (TBool) override { return kResultOk; }
    tresult PLUGIN_API setState (IBStream* stream) override
    {
        if (crashMode ("state"))
            nullWrite();
        char data[4] {};
        int32 read = 0;
        if (stream != nullptr)
            stream->read (data, 4, &read);
        return kResultOk;
    }
    tresult PLUGIN_API getState (IBStream* stream) override
    {
        int32 written = 0;
        return stream != nullptr ? stream->write (const_cast<char*> ("LXV1"), 4, &written) : kInvalidArgument;
    }

    // IAudioProcessor
    tresult PLUGIN_API setBusArrangements (SpeakerArrangement* ins, int32 numIns, SpeakerArrangement* outs, int32 numOuts) override
    {
        return numIns == 1 && numOuts == 1 && ins[0] == SpeakerArr::kStereo && outs[0] == SpeakerArr::kStereo ? kResultTrue : kResultFalse;
    }
    tresult PLUGIN_API getBusArrangement (BusDirection, int32 index, SpeakerArrangement& arr) override
    {
        if (index != 0)
            return kInvalidArgument;
        arr = SpeakerArr::kStereo;
        return kResultOk;
    }
    tresult PLUGIN_API canProcessSampleSize (int32 size) override { return size == kSample32 ? kResultTrue : kResultFalse; }
    uint32 PLUGIN_API getLatencySamples() override { return 0; }
    tresult PLUGIN_API setupProcessing (ProcessSetup&) override { return kResultOk; }
    tresult PLUGIN_API setProcessing (TBool) override { return kResultOk; }
    uint32 PLUGIN_API getTailSamples() override { return 0; }

    tresult PLUGIN_API process (ProcessData& data) override
    {
        const auto n = data.numSamples;
        if (n <= 0 || data.numOutputs < 1 || data.outputs == nullptr || data.outputs[0].numChannels < 2)
            return kResultOk; // (a parameter flush)
        if (++processCalls == crashAfter())
        {
            if (crashMode ("process"))
                nullWrite();
            if (crashMode ("memcpy"))
                nullCopy();
#if ! defined(_WIN32)
            if (crashMode ("thread"))
                startFaultingThread();
#endif
        }
        auto& out = data.outputs[0];
        for (int c = 0; c < 2; ++c)
        {
            auto* to = out.channelBuffers32[c];
            const auto* from = data.numInputs > 0 && data.inputs != nullptr && data.inputs[0].numChannels > c
                                       && data.inputs[0].channelBuffers32 != nullptr
                                   ? data.inputs[0].channelBuffers32[c]
                                   : nullptr;
            if (to == nullptr)
                continue;
            for (int32 s = 0; s < n; ++s)
                to[s] = from != nullptr ? 0.5f * from[s] : 0.0f;
        }
        return kResultOk;
    }

    // IEditController (no parameters, no editor)
    tresult PLUGIN_API setComponentState (IBStream*) override { return kResultOk; }
    int32 PLUGIN_API getParameterCount() override { return 0; }
    tresult PLUGIN_API getParameterInfo (int32, ParameterInfo&) override { return kInvalidArgument; }
    tresult PLUGIN_API getParamStringByValue (ParamID, ParamValue, String128) override { return kInvalidArgument; }
    tresult PLUGIN_API getParamValueByString (ParamID, TChar*, ParamValue&) override { return kInvalidArgument; }
    ParamValue PLUGIN_API normalizedParamToPlain (ParamID, ParamValue v) override { return v; }
    ParamValue PLUGIN_API plainParamToNormalized (ParamID, ParamValue v) override { return v; }
    ParamValue PLUGIN_API getParamNormalized (ParamID) override { return 0.0; }
    tresult PLUGIN_API setParamNormalized (ParamID, ParamValue) override { return kResultOk; }
    tresult PLUGIN_API setComponentHandler (IComponentHandler*) override { return kResultOk; }
    IPlugView* PLUGIN_API createView (FIDString) override { return nullptr; }

private:
    std::atomic<uint32> refs { 1 };
};

class Factory final : public IPluginFactory2
{
public:
    tresult PLUGIN_API queryInterface (const TUID iid, void** obj) override
    {
        if (FUnknownPrivate::iidEqual (iid, FUnknown::iid) || FUnknownPrivate::iidEqual (iid, IPluginFactory::iid)
            || FUnknownPrivate::iidEqual (iid, IPluginFactory2::iid))
        {
            *obj = static_cast<IPluginFactory2*> (this);
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }

    tresult PLUGIN_API getFactoryInfo (PFactoryInfo* info) override
    {
        std::memset (static_cast<void*> (info), 0, sizeof (*info));
        std::strcpy (info->vendor, "LYNX");
        std::strcpy (info->url, "https://github.com/Ka1-S/LYNX-SHARE");
        info->flags = PFactoryInfo::kUnicode;
        return kResultOk;
    }
    int32 PLUGIN_API countClasses() override { return 1; }
    tresult PLUGIN_API getClassInfo (int32 index, PClassInfo* info) override
    {
        if (index != 0)
            return kInvalidArgument;
        std::memset (static_cast<void*> (info), 0, sizeof (*info));
        effectUid.toTUID (info->cid);
        info->cardinality = PClassInfo::kManyInstances;
        std::strcpy (info->category, kVstAudioEffectClass);
        std::strcpy (info->name, "LYNX Test Crash VST3");
        return kResultOk;
    }
    tresult PLUGIN_API getClassInfo2 (int32 index, PClassInfo2* info) override
    {
        if (index != 0)
            return kInvalidArgument;
        std::memset (static_cast<void*> (info), 0, sizeof (*info));
        effectUid.toTUID (info->cid);
        info->cardinality = PClassInfo::kManyInstances;
        std::strcpy (info->category, kVstAudioEffectClass);
        std::strcpy (info->name, "LYNX Test Crash VST3");
        std::strcpy (info->subCategories, "Fx");
        std::strcpy (info->vendor, "LYNX");
        std::strcpy (info->version, "1.0.0");
        std::strcpy (info->sdkVersion, "VST 3.7");
        return kResultOk;
    }
    tresult PLUGIN_API createInstance (FIDString cid, FIDString iid, void** obj) override
    {
        if (! FUnknownPrivate::iidEqual (cid, effectUid.toTUID()))
        {
            *obj = nullptr;
            return kNoInterface;
        }
        auto* effect = new TestCrash();
        const auto result = effect->queryInterface (iid, obj);
        effect->release();
        return result;
    }
};

Factory factory;
} // namespace

extern "C"
{
LYNX_TEST_EXPORT IPluginFactory* PLUGIN_API GetPluginFactory() { return static_cast<IPluginFactory2*> (&factory); }
#if defined(_WIN32)
LYNX_TEST_EXPORT bool InitDll() { return true; }
LYNX_TEST_EXPORT bool ExitDll() { return true; }
#else
LYNX_TEST_EXPORT bool ModuleEntry (void*) { return true; }
LYNX_TEST_EXPORT bool ModuleExit() { return true; }
#endif
}
