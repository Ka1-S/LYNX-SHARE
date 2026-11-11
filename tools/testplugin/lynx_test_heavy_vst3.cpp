// A heavy VST3 instrument for measuring what Freeze unloads (round 190ay). The test synth of lynx_test_synth_vst3.cpp
// (same build line, output "LYNX Test Heavy.vst3") made expensive on purpose, with nothing to click and no licence:
//   - memory: each instance allocates and touches LYNX_TEST_HEAVY_MB megabytes (default 256), like a sample library;
//   - CPU: LYNX_TEST_HEAVY_WORK extra sine evaluations per frame (default 400), folded into the sound at -140 dB;
//   - latency: it reports LYNX_TEST_HEAVY_LATENCY frames (default 512) and its output comes that much late;
//   - state: a detune in cents. A new instance starts at 7 cents x its number in the process, so two instances only
//     sound the same when the host gave the second one the first one's state (getState / setState);
//   - with LYNX_TEST_HEAVY_LOG = a file path it appends "create <n>" / "destroy <n>" lines: which instances are alive.
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

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace
{
const FUID synthUid (0x4C594E58, 0x48565931, 0x90A70001, 0x00000001); // "LYNXTSY169c1VST3"

void copyString (String128 to, const char* from)
{
    int i = 0;
    for (; from[i] != 0 && i < 127; ++i)
        to[i] = (char16) from[i];
    to[i] = 0;
}

class TestHeavy final : public IComponent, public IAudioProcessor, public IEditController
{
public:
    TestHeavy()
    {
        auto number = [] (const char* name, long fallback) {
            const char* v = std::getenv (name);
            return v != nullptr && *v != 0 ? std::atol (v) : fallback;
        };
        megabytes = (size_t) std::max (0L, number ("LYNX_TEST_HEAVY_MB", 256));
        work = (int) std::max (0L, number ("LYNX_TEST_HEAVY_WORK", 400));
        latency = (int) std::min (8192L, std::max (0L, number ("LYNX_TEST_HEAVY_LATENCY", 512)));
        library = (unsigned char*) std::malloc (megabytes * 1048576 + 1);
        if (library != nullptr)
            for (size_t i = 0; i < megabytes * 1048576; i += 4096)
                library[i] = (unsigned char) (i >> 12); // touched: really in memory
        cents = 7.0f * (float) id;
        note ("create");
    }
    virtual ~TestHeavy()
    {
        note ("destroy");
        std::free (library);
        if (log != nullptr)
            std::fclose (log);
    }
    void note (const char* what) const
    {
        if (const char* path = std::getenv ("LYNX_TEST_HEAVY_LOG"); path != nullptr && *path != 0)
            if (auto* f = std::fopen (path, "a"))
            {
                std::fprintf (f, "%s %d\n", what, id);
                std::fclose (f);
            }
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
            if (const char* path = std::getenv ("LYNX_TEST_HEAVY_PROCESS_LOG"); path != nullptr && *path != 0)
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
        {
            voiceCount = 0;
            std::memset (line, 0, sizeof (line));
            linePos = 0;
        }
        return kResultOk;
    }
    tresult PLUGIN_API setState (IBStream* s) override
    {
        char tag[4] {};
        float value = 0.0f;
        int32 got = 0;
        if (s != nullptr && s->read (tag, 4, &got) == kResultOk && got == 4 && std::memcmp (tag, "LTH1", 4) == 0
            && s->read (&value, 4, &got) == kResultOk && got == 4)
            cents = value;
        return kResultOk;
    }
    tresult PLUGIN_API getState (IBStream* s) override
    {
        if (s != nullptr)
        {
            int32 written = 0;
            const char tag[4] = { 'L', 'T', 'H', '1' };
            s->write ((void*) tag, 4, &written);
            s->write ((void*) &cents, 4, &written);
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
    uint32 PLUGIN_API getLatencySamples() override { return (uint32) latency; }
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
            // the cost: `work` sines per frame, 140 dB down
            double burn = 0.0;
            for (int w = 0; w < work; ++w)
                burn += std::sin (burnPhase + 0.001 * (double) w);
            burnPhase += 0.01;
            if (burnPhase > 6.283185307179586)
                burnPhase -= 6.283185307179586;
            v += (float) (1.0e-7 * burn / (double) (work > 0 ? work : 1));
            if (library != nullptr && megabytes > 0)
                v += 1.0e-9f * (float) library[(size_t) (burnPhase * 1000.0) * 4096 % (megabytes * 1048576)];
            // the latency: out comes what went in `latency` frames ago
            if (latency > 0)
            {
                const auto delayed = line[linePos];
                line[linePos] = v;
                linePos = (linePos + 1) % latency;
                v = delayed;
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
        double phase = 0.0, step = 0.0;
    };

    void apply (const Event& e)
    {
        if (e.type == Event::kNoteOnEvent)
        {
            if (e.noteOn.velocity <= 0.0f)
            {
                noteOff (e.noteOn.pitch, e.sampleOffset);
                return;
            }
            if (log != nullptr)
                std::fprintf (log, "E%d on %d %d\n", id, (int) e.noteOn.pitch, (int) e.sampleOffset);
            if (voiceCount < maxVoices)
            {
                auto& v = voices[voiceCount++];
                v.key = e.noteOn.pitch;
                v.phase = 0.0;
                v.step = 6.283185307179586 * 440.0 * std::pow (2.0, (e.noteOn.pitch - 69 + cents / 100.0) / 12.0) / rate;
            }
        }
        else if (e.type == Event::kNoteOffEvent)
            noteOff (e.noteOff.pitch, e.sampleOffset);
    }

    void noteOff (int key, int offset)
    {
        if (log != nullptr)
            std::fprintf (log, "E%d off %d %d\n", id, key, offset);
        for (int i = 0; i < voiceCount; ++i)
            if (voices[i].key == key)
            {
                for (int j = i + 1; j < voiceCount; ++j)
                    voices[j - 1] = voices[j];
                --voiceCount;
                return;
            }
    }

    size_t megabytes = 0;
    unsigned char* library = nullptr;
    int work = 0, latency = 0, linePos = 0;
    float line[8192] {};
    double burnPhase = 0.0;
    float cents = 0.0f;
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
        std::strcpy (info->name, "LYNX Test Heavy");
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
        std::strcpy (info->name, "LYNX Test Heavy");
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
        auto* synth = new TestHeavy();
        const auto result = synth->queryInterface (iid, obj);
        synth->release();
        return result;
    }
};

Factory factory;
} // namespace

extern "C"
{
__declspec (dllexport) IPluginFactory* PLUGIN_API GetPluginFactory() { return static_cast<IPluginFactory2*> (&factory); }
__declspec (dllexport) bool InitDll() { return true; }
__declspec (dllexport) bool ExitDll() { return true; }
}
