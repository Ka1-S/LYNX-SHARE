// A VST3 effect with one parameter for timing tests (round 190jg, sample-accurate automation): it ignores its input and
// outputs its "Level" parameter as a constant (DC) on both channels, applying each point of the parameter's
// IParamValueQueue at the point's sample offset. A host that sends a step of the automation at its sample shows the
// step at that sample in the output; one that sends a value per block shows it at the block's start. With the
// environment variable LYNX_TEST_PARAM_LOG = a file path it appends one line per point received ("P <offset> <value>").
// Built only against the VST3 SDK's pluginterfaces (no SDK helper classes), like lynx_test_crash_vst3.cpp:
//   V=<JUCE>/modules/juce_audio_processors/format_types/VST3_SDK
//   Linux:   B="LYNX Test Param.vst3/Contents/x86_64-linux"; mkdir -p "$B"
//            c++ -shared -fPIC -O2 -std=c++17 -I$V tools/testplugin/lynx_test_param_vst3.cpp
//                $V/pluginterfaces/base/funknown.cpp -o "$B/LYNX Test Param.so"                  (one line)
//   Windows: zig c++ -shared -O2 -std=c++17 -target x86_64-windows-gnu -I$V tools/testplugin/lynx_test_param_vst3.cpp
//                $V/pluginterfaces/base/funknown.cpp -lole32 -o "LYNX Test Param.vst3"           (one line)
//   then:    LYNX --automationtimingtest "LYNX Test Param.vst3" report.txt
#define INIT_CLASS_IID
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/gui/iplugview.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

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
const FUID effectUid (0x4C594E58, 0x50415241, 0x4D313930, 0x4A475633); // "LYNXPARAM190JGV3"

void copyString (String128 to, const char* from)
{
    int i = 0;
    for (; from[i] != 0 && i < 127; ++i)
        to[i] = (char16) from[i];
    to[i] = 0;
}

FILE* logFile()
{
    static FILE* f = [] () -> FILE* {
#if defined(_WIN32)
        char path[1024] {};
        return GetEnvironmentVariableA ("LYNX_TEST_PARAM_LOG", path, sizeof (path)) > 0 ? std::fopen (path, "a") : nullptr;
#else
        const char* path = std::getenv ("LYNX_TEST_PARAM_LOG");
        return path != nullptr ? std::fopen (path, "a") : nullptr;
#endif
    }();
    return f;
}

class TestParam final : public IComponent, public IAudioProcessor, public IEditController
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

    tresult PLUGIN_API initialize (FUnknown*) override { return kResultOk; }
    tresult PLUGIN_API terminate() override { return kResultOk; }

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
        float v = 0.5f;
        int32 read = 0;
        if (stream != nullptr && stream->read (&v, 4, &read) == kResultOk && read == 4)
            level = v;
        return kResultOk;
    }
    tresult PLUGIN_API getState (IBStream* stream) override
    {
        int32 written = 0;
        float v = (float) level;
        return stream != nullptr ? stream->write (&v, 4, &written) : kInvalidArgument;
    }

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
        // the points of parameter 0 in this block (at most 64), applied at their offsets
        int32 offsets[64];
        double values[64];
        int32 points = 0;
        if (auto* changes = data.inputParameterChanges)
            for (int32 i = 0; i < changes->getParameterCount(); ++i)
                if (auto* queue = changes->getParameterData (i); queue != nullptr && queue->getParameterId() == 0)
                    for (int32 k = 0; k < queue->getPointCount() && points < 64; ++k)
                        if (queue->getPoint (k, offsets[points], values[points]) == kResultOk)
                        {
                            if (FILE* f = logFile())
                                std::fprintf (f, "P %d %.6f n=%d\n", (int) offsets[points], values[points], (int) data.numSamples);
                            ++points;
                        }
        if (FILE* f = logFile())
            std::fflush (f);
        const auto n = data.numSamples;
        if (n <= 0 || data.numOutputs < 1 || data.outputs == nullptr || data.outputs[0].numChannels < 2)
        {
            if (points > 0)
                level = values[points - 1]; // (a parameter flush)
            return kResultOk;
        }
        auto& out = data.outputs[0];
        int32 next = 0;
        for (int32 s = 0; s < n; ++s)
        {
            while (next < points && offsets[next] <= s)
                level = values[next++];
            for (int c = 0; c < 2; ++c)
                if (out.channelBuffers32[c] != nullptr)
                    out.channelBuffers32[c][s] = (float) level;
        }
        while (next < points)
            level = values[next++];
        return kResultOk;
    }

    // IEditController: one parameter, "Level" 0..1
    tresult PLUGIN_API setComponentState (IBStream*) override { return kResultOk; }
    int32 PLUGIN_API getParameterCount() override { return 1; }
    tresult PLUGIN_API getParameterInfo (int32 index, ParameterInfo& info) override
    {
        if (index != 0)
            return kInvalidArgument;
        std::memset (&info, 0, sizeof (info));
        info.id = 0;
        copyString (info.title, "Level");
        copyString (info.shortTitle, "Level");
        info.stepCount = 0;
        info.defaultNormalizedValue = 0.5;
        info.flags = ParameterInfo::kCanAutomate;
        return kResultOk;
    }
    tresult PLUGIN_API getParamStringByValue (ParamID, ParamValue v, String128 s) override
    {
        char text[32];
        std::snprintf (text, sizeof (text), "%.3f", v);
        copyString (s, text);
        return kResultOk;
    }
    tresult PLUGIN_API getParamValueByString (ParamID, TChar*, ParamValue&) override { return kInvalidArgument; }
    ParamValue PLUGIN_API normalizedParamToPlain (ParamID, ParamValue v) override { return v; }
    ParamValue PLUGIN_API plainParamToNormalized (ParamID, ParamValue v) override { return v; }
    ParamValue PLUGIN_API getParamNormalized (ParamID id) override { return id == 0 ? controllerValue : 0.0; }
    tresult PLUGIN_API setParamNormalized (ParamID id, ParamValue v) override
    {
        if (id == 0)
            controllerValue = v;
        return kResultOk;
    }
    tresult PLUGIN_API setComponentHandler (IComponentHandler*) override { return kResultOk; }
    IPlugView* PLUGIN_API createView (FIDString) override { return nullptr; }

private:
    std::atomic<uint32> refs { 1 };
    double level = 0.5, controllerValue = 0.5;
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
        std::strcpy (info->name, "LYNX Test Param");
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
        std::strcpy (info->name, "LYNX Test Param");
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
        auto* effect = new TestParam();
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
