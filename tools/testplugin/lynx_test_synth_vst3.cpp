// A minimal VST3 instrument for checking what LYNX sends a hosted synth (round 169c, issue #32). It is built only
// against the VST3 SDK's pluginterfaces (no SDK helper classes) and on purpose has NO IMidiMapping, like many
// third-party synths (KV331 SynthMaster 2): a MIDI controller such as All Notes Off (CC 123) can never reach it.
//
//   V=<JUCE>/modules/juce_audio_processors/format_types/VST3_SDK
//   zig c++ -shared -O2 -std=c++17 -target x86_64-windows-gnu -I$V tools/testplugin/lynx_test_synth_vst3.cpp \
//       $V/pluginterfaces/base/funknown.cpp -lole32 -o "LYNX Test Synth.vst3"
//   Linux (round 190fg): g++ -shared -fPIC -O2 -std=c++17 -I$V tools/testplugin/lynx_test_synth_vst3.cpp \
//       $V/pluginterfaces/base/funknown.cpp -o "LYNX Test Synth.vst3/Contents/x86_64-linux/LYNX Test Synth.so"
//
// Sound: one sine voice per note on (a second note on of a sounding key adds a second voice, as a polyphonic synth
// does). The voices are kept by noteId, the VST3 way (round 190fg): a note off with an id ends the voice of that id;
// without one (-1) it ends the oldest voice of its key. With the environment variable LYNX_TEST_SYNTH_LOG = a file path
// it appends to <path>.<instance> one line per process call ("P<instance> <ppq> <samples> <playing> <rt|off> <voices>") and per
// event ("E<instance> on|off <key> <sampleOffset> <noteId>"): what the host sent and when.
#define INIT_CLASS_IID
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/gui/iplugview.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace
{
const FUID synthUid (0x4C594E58, 0x54535931, 0x36396331, 0x56535433); // "LYNXTSY169c1VST3"

void copyString (String128 to, const char* from)
{
    int i = 0;
    for (; from[i] != 0 && i < 127; ++i)
        to[i] = (char16) from[i];
    to[i] = 0;
}

class TestSynth final : public IComponent, public IAudioProcessor, public IEditController
{
public:
    TestSynth() = default;
    virtual ~TestSynth()
    {
        if (log != nullptr)
            std::fclose (log);
    }

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

    // IPluginBase (shared by the component and the controller)
    tresult PLUGIN_API initialize (FUnknown*) override
    {
        if (log == nullptr)
            if (const char* path = std::getenv ("LYNX_TEST_SYNTH_LOG"); path != nullptr && *path != 0)
            {
                char name[1024];
                std::snprintf (name, sizeof (name), "%s.%d", path, id); // one file per instance (threads)
                log = std::fopen (name, "a");
            }
        return kResultOk;
    }
    tresult PLUGIN_API terminate() override { return kResultOk; }

    // IComponent
    tresult PLUGIN_API getControllerClassId (TUID) override { return kNotImplemented; } // single component
    tresult PLUGIN_API setIoMode (IoMode) override { return kResultOk; }
    int32 PLUGIN_API getBusCount (MediaType type, BusDirection dir) override
    {
        if (type == kAudio && dir == kOutput)
            return 1;
        if (type == kEvent && dir == kInput)
            return 1;
        return 0;
    }
    tresult PLUGIN_API getBusInfo (MediaType type, BusDirection dir, int32 index, BusInfo& bus) override
    {
        if (index != 0)
            return kInvalidArgument;
        std::memset (&bus, 0, sizeof (bus));
        bus.mediaType = type;
        bus.direction = dir;
        bus.busType = kMain;
        bus.flags = BusInfo::kDefaultActive;
        if (type == kAudio && dir == kOutput)
        {
            bus.channelCount = 2;
            copyString (bus.name, "Out");
            return kResultOk;
        }
        if (type == kEvent && dir == kInput)
        {
            bus.channelCount = 16;
            copyString (bus.name, "MIDI In");
            return kResultOk;
        }
        return kInvalidArgument;
    }
    tresult PLUGIN_API getRoutingInfo (RoutingInfo&, RoutingInfo&) override { return kNotImplemented; }
    tresult PLUGIN_API activateBus (MediaType, BusDirection, int32, TBool) override { return kResultOk; }
    tresult PLUGIN_API setActive (TBool state) override
    {
        if (! state)
            voiceCount = 0;
        return kResultOk;
    }
    tresult PLUGIN_API setState (IBStream*) override { return kResultOk; }
    tresult PLUGIN_API getState (IBStream* s) override
    {
        if (s != nullptr)
        {
            int32 written = 0;
            const char tag[4] = { 'L', 'T', 'S', '1' };
            s->write ((void*) tag, 4, &written);
        }
        return kResultOk;
    }

    // IAudioProcessor
    tresult PLUGIN_API setBusArrangements (SpeakerArrangement*, int32 numIns, SpeakerArrangement* outs, int32 numOuts) override
    {
        return numIns == 0 && numOuts == 1 && outs[0] == SpeakerArr::kStereo ? kResultTrue : kResultFalse;
    }
    tresult PLUGIN_API getBusArrangement (BusDirection dir, int32 index, SpeakerArrangement& arr) override
    {
        if (dir == kOutput && index == 0)
        {
            arr = SpeakerArr::kStereo;
            return kResultOk;
        }
        return kInvalidArgument;
    }
    tresult PLUGIN_API canProcessSampleSize (int32 size) override { return size == kSample32 ? kResultTrue : kResultFalse; }
    uint32 PLUGIN_API getLatencySamples() override { return 0; }
    tresult PLUGIN_API setupProcessing (ProcessSetup& setup) override
    {
        rate = setup.sampleRate > 0.0 ? setup.sampleRate : 44100.0;
        offline = setup.processMode == kOffline;
        return kResultOk;
    }
    tresult PLUGIN_API setProcessing (TBool) override { return kResultOk; }
    uint32 PLUGIN_API getTailSamples() override { return 0; }

    tresult PLUGIN_API process (ProcessData& data) override
    {
        const auto n = data.numSamples;
        if (log != nullptr)
        {
            const auto* c = data.processContext;
            const auto playing = c != nullptr && (c->state & ProcessContext::kPlaying) != 0;
            const auto ppq = c != nullptr && (c->state & ProcessContext::kProjectTimeMusicValid) != 0 ? c->projectTimeMusic : -1.0;
            std::fprintf (log, "P%d %.9f %d %d %s %d\n", id, ppq, (int) n, playing ? 1 : 0, offline ? "off" : "rt", voiceCount);
        }

        float* left = nullptr;
        float* right = nullptr;
        if (data.numOutputs > 0 && data.outputs[0].numChannels >= 2 && n > 0)
        {
            left = data.outputs[0].channelBuffers32[0];
            right = data.outputs[0].channelBuffers32[1];
            std::memset (left, 0, sizeof (float) * (size_t) n);
            std::memset (right, 0, sizeof (float) * (size_t) n);
        }

        auto* events = data.inputEvents;
        const auto eventCount = events != nullptr ? events->getEventCount() : 0;
        int32 next = 0;
        Event e {};
        auto take = [&] (int32 i) { return events != nullptr && events->getEvent (i, e) == kResultOk; };
        bool have = next < eventCount && take (next);

        for (int32 s = 0; s < n || (have && n == 0); ++s)
        {
            while (have && e.sampleOffset <= s)
            {
                apply (e);
                ++next;
                have = next < eventCount && take (next);
            }
            if (n == 0)
                break;
            float v = 0.0f;
            for (int i = 0; i < voiceCount; ++i)
            {
                auto& voice = voices[i];
                v += 0.1f * (float) std::sin (voice.phase);
                voice.phase += voice.step;
                if (voice.phase > 6.283185307179586)
                    voice.phase -= 6.283185307179586;
            }
            if (left != nullptr)
            {
                left[s] = v;
                right[s] = v;
            }
        }
        while (have) // (events past the block's end)
        {
            apply (e);
            ++next;
            have = next < eventCount && take (next);
        }
        if (log != nullptr && eventCount > 0)
            std::fflush (log);
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
    struct Voice
    {
        int key = 0;
        int32 noteId = -1;
        double phase = 0.0, step = 0.0;
    };

    void apply (const Event& e)
    {
        if (e.type == Event::kNoteOnEvent)
        {
            if (e.noteOn.velocity <= 0.0f)
            {
                noteOff (e.noteOn.pitch, e.sampleOffset, -1);
                return;
            }
            if (log != nullptr)
                std::fprintf (log, "E%d on %d %d %d\n", id, (int) e.noteOn.pitch, (int) e.sampleOffset, (int) e.noteOn.noteId);
            if (voiceCount < maxVoices)
            {
                auto& v = voices[voiceCount++];
                v.key = e.noteOn.pitch;
                v.noteId = e.noteOn.noteId;
                v.phase = 0.0;
                v.step = 6.283185307179586 * 440.0 * std::pow (2.0, (e.noteOn.pitch - 69) / 12.0) / rate;
            }
        }
        else if (e.type == Event::kNoteOffEvent)
            noteOff (e.noteOff.pitch, e.sampleOffset, e.noteOff.noteId);
    }

    void noteOff (int key, int offset, int32 noteId)
    {
        if (log != nullptr)
            std::fprintf (log, "E%d off %d %d %d\n", id, key, offset, (int) noteId);
        for (int i = 0; i < voiceCount; ++i)
            if (noteId >= 0 ? voices[i].noteId == noteId : voices[i].key == key)
            {
                for (int j = i + 1; j < voiceCount; ++j)
                    voices[j - 1] = voices[j];
                --voiceCount;
                return;
            }
    }

    static constexpr int maxVoices = 64;
    Voice voices[maxVoices];
    int voiceCount = 0;
    double rate = 44100.0;
    bool offline = false;
    std::FILE* log = nullptr;
    std::atomic<uint32> refs { 1 };
    static inline std::atomic<int> made { 0 };
    const int id = ++made; // (each instance's lines carry its number)
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
        synthUid.toTUID (info->cid);
        info->cardinality = PClassInfo::kManyInstances;
        std::strcpy (info->category, kVstAudioEffectClass);
        std::strcpy (info->name, "LYNX Test Synth");
        return kResultOk;
    }
    tresult PLUGIN_API getClassInfo2 (int32 index, PClassInfo2* info) override
    {
        if (index != 0)
            return kInvalidArgument;
        std::memset (info, 0, sizeof (*info));
        synthUid.toTUID (info->cid);
        info->cardinality = PClassInfo::kManyInstances;
        std::strcpy (info->category, kVstAudioEffectClass);
        std::strcpy (info->name, "LYNX Test Synth");
        std::strcpy (info->subCategories, "Instrument|Synth");
        std::strcpy (info->vendor, "LYNX");
        std::strcpy (info->version, "1.0.0");
        std::strcpy (info->sdkVersion, "VST 3.7");
        return kResultOk;
    }
    tresult PLUGIN_API createInstance (FIDString cid, FIDString iid, void** obj) override
    {
        if (! FUnknownPrivate::iidEqual (cid, synthUid.toTUID()))
        {
            *obj = nullptr;
            return kNoInterface;
        }
        auto* synth = new TestSynth();
        const auto result = synth->queryInterface (iid, obj);
        synth->release();
        return result;
    }
};

Factory factory;
} // namespace

extern "C"
{
#if defined(_WIN32)
__declspec (dllexport) IPluginFactory* PLUGIN_API GetPluginFactory() { return static_cast<IPluginFactory2*> (&factory); }
__declspec (dllexport) bool InitDll() { return true; }
__declspec (dllexport) bool ExitDll() { return true; }
#else
// (round 190fg: a Linux build, the bundle's Contents/x86_64-linux/<name>.so)
__attribute__ ((visibility ("default"))) IPluginFactory* PLUGIN_API GetPluginFactory() { return static_cast<IPluginFactory2*> (&factory); }
__attribute__ ((visibility ("default"))) bool ModuleEntry (void*) { return true; }
__attribute__ ((visibility ("default"))) bool ModuleExit() { return true; }
#endif
}
