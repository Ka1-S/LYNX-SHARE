// A VST3 effect whose sidechain input behaves like every nih-plug VST3 (issue #155, Spectral Compressor): it has a
// stereo main input and output plus a stereo "Sidechain" aux input, ignores activateBus (all buses default active)
// and, in process, reads every channel of every input bus the host passes. The VST3 SDK allows null sample pointers
// for an inactive bus; nih-plug copies from them anyway (memcpy from null: SIGSEGV on the host's audio thread). This
// plugin does not crash: a null channel pointer on any input bus makes its output silent (and it says so on stderr
// once), so a host that passes one fails `LYNX --plugintest` (peak 0) instead of dying. Otherwise main in -> out.
// Built only against the VST3 SDK's pluginterfaces (no SDK helper classes).
//
//   V=<JUCE>/modules/juce_audio_processors/format_types/VST3_SDK
//   Linux:   B="LYNX Test Sidechain.vst3/Contents/x86_64-linux"; mkdir -p "$B"
//            c++ -shared -fPIC -O2 -std=c++17 -I$V tools/testplugin/lynx_test_sidechain_vst3.cpp
//                $V/pluginterfaces/base/funknown.cpp -o "$B/LYNX Test Sidechain.so"           (one line)
//   Windows: zig c++ -shared -O2 -std=c++17 -target x86_64-windows-gnu -I$V tools/testplugin/lynx_test_sidechain_vst3.cpp
//                $V/pluginterfaces/base/funknown.cpp -lole32 -o "LYNX Test Sidechain.vst3"      (one line)
//   then:    LYNX --plugintest "LYNX Test Sidechain.vst3" report.txt   (result=OK; FAILED with peak=0 on null pointers)
#define INIT_CLASS_IID
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/gui/iplugview.h"

#include <atomic>
#include <cstdio>
#include <cstring>

#if defined(_WIN32)
 #define LYNX_TEST_EXPORT __declspec (dllexport)
#else
 #define LYNX_TEST_EXPORT __attribute__ ((visibility ("default")))
#endif

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace
{
const FUID effectUid (0x4C594E58, 0x53494443, 0x31353500, 0x56535433); // "LYNXSIDC155"

void copyString (String128 to, const char* from)
{
    int i = 0;
    for (; from[i] != 0 && i < 127; ++i)
        to[i] = (char16) from[i];
    to[i] = 0;
}

class TestSidechain final : public IComponent, public IAudioProcessor, public IEditController
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

    // IComponent: main in (0) + "Sidechain" aux in (1), main out (0); all default active, like nih-plug
    tresult PLUGIN_API getControllerClassId (TUID) override { return kNotImplemented; } // single component
    tresult PLUGIN_API setIoMode (IoMode) override { return kResultOk; }
    int32 PLUGIN_API getBusCount (MediaType type, BusDirection dir) override
    {
        if (type != kAudio)
            return 0;
        return dir == kInput ? 2 : 1;
    }
    tresult PLUGIN_API getBusInfo (MediaType type, BusDirection dir, int32 index, BusInfo& bus) override
    {
        if (type != kAudio || index < 0 || index >= getBusCount (type, dir))
            return kInvalidArgument;
        std::memset (&bus, 0, sizeof (bus));
        bus.mediaType = type;
        bus.direction = dir;
        bus.channelCount = 2;
        bus.busType = index == 0 ? kMain : kAux;
        bus.flags = BusInfo::kDefaultActive;
        copyString (bus.name, dir == kOutput ? "Output" : index == 0 ? "Input" : "Sidechain");
        return kResultOk;
    }
    tresult PLUGIN_API getRoutingInfo (RoutingInfo&, RoutingInfo&) override { return kNotImplemented; }
    // nih-plug: "We don't support this, but the validator will get very angry with us if we let it know that"
    tresult PLUGIN_API activateBus (MediaType, BusDirection, int32, TBool) override { return kResultOk; }
    tresult PLUGIN_API setActive (TBool) override { return kResultOk; }
    tresult PLUGIN_API setState (IBStream*) override { return kResultOk; }
    tresult PLUGIN_API getState (IBStream*) override { return kResultOk; }

    // IAudioProcessor
    tresult PLUGIN_API setBusArrangements (SpeakerArrangement* ins, int32 numIns, SpeakerArrangement* outs, int32 numOuts) override
    {
        if (numIns != 2 || numOuts != 1)
            return kResultFalse;
        return ins[0] == SpeakerArr::kStereo && ins[1] == SpeakerArr::kStereo && outs[0] == SpeakerArr::kStereo ? kResultTrue
                                                                                                               : kResultFalse;
    }
    tresult PLUGIN_API getBusArrangement (BusDirection dir, int32 index, SpeakerArrangement& arr) override
    {
        if (index < 0 || index >= getBusCount (kAudio, dir))
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

        // what nih-plug does with each input bus: copy every channel the host announces (here: just read it)
        auto nullBus = -1;
        float sum = 0.0f;
        for (int32 b = 0; b < data.numInputs && data.inputs != nullptr; ++b)
        {
            const auto& bus = data.inputs[b];
            if (bus.numChannels > 0 && bus.channelBuffers32 == nullptr)
            {
                nullBus = b;
                continue;
            }
            for (int32 c = 0; c < bus.numChannels; ++c)
            {
                const auto* samples = bus.channelBuffers32[c];
                if (samples == nullptr)
                {
                    nullBus = b;
                    break;
                }
                for (int32 s = 0; s < n; ++s)
                    sum += samples[s];
            }
        }
        sideSum = sum; // (kept, so reading the samples is not optimised away)

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
            if (nullBus >= 0 || from == nullptr)
                std::memset (to, 0, sizeof (float) * (size_t) n);
            else if (from != to)
                std::memcpy (to, from, sizeof (float) * (size_t) n);
        }
        if (nullBus >= 0 && ! told.exchange (true))
            std::fprintf (stderr, "LYNX Test Sidechain: null sample pointers on input bus %d (nih-plug would crash here)\n", (int) nullBus);
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
    std::atomic<bool> told { false };
    volatile float sideSum = 0.0f;
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
        std::memset (info, 0, sizeof (*info));
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
        std::memset (info, 0, sizeof (*info));
        effectUid.toTUID (info->cid);
        info->cardinality = PClassInfo::kManyInstances;
        std::strcpy (info->category, kVstAudioEffectClass);
        std::strcpy (info->name, "LYNX Test Sidechain");
        return kResultOk;
    }
    tresult PLUGIN_API getClassInfo2 (int32 index, PClassInfo2* info) override
    {
        if (index != 0)
            return kInvalidArgument;
        std::memset (info, 0, sizeof (*info));
        effectUid.toTUID (info->cid);
        info->cardinality = PClassInfo::kManyInstances;
        std::strcpy (info->category, kVstAudioEffectClass);
        std::strcpy (info->name, "LYNX Test Sidechain");
        std::strcpy (info->subCategories, "Fx|Dynamics");
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
        auto* effect = new TestSidechain();
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
