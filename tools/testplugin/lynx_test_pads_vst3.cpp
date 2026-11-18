// A drum-pad VST3 instrument with clickable pads and a MIDI output (round 190ks, GitHub #198): a hit on a pad in the
// plugin's own window plays the pad AND sends the pad's note out on the plugin's event output bus, as drum samplers with
// "MIDI out" do (the notes a host may record while recording). Built only against the VST3 SDK's pluginterfaces, like
// lynx_test_synth_vst3.cpp:
//
//   V=<JUCE>/modules/juce_audio_processors/format_types/VST3_SDK
//   Windows: zig c++ -shared -O2 -std=c++17 -target x86_64-windows-gnu -I$V tools/testplugin/lynx_test_pads_vst3.cpp (cont.)
//                $V/pluginterfaces/base/funknown.cpp -lole32 -luser32 -lgdi32 -o "LYNX Test Pads.vst3"
//   Linux:   B="LYNX Test Pads.vst3/Contents/x86_64-linux"; mkdir -p "$B"
//            c++ -shared -fPIC -O2 -std=c++17 -I$V tools/testplugin/lynx_test_pads_vst3.cpp (cont.)
//                $V/pluginterfaces/base/funknown.cpp -o "$B/LYNX Test Pads.so"
//
// Pads: 8 (two rows of four), keys 36..43 (Kick, Snare, Clap, Hat, Tom 1, Tom 2, Ride, Crash). A pad sounds a decaying
// sine at 110 × (k + 1) Hz, from a played note or a click. The window (Windows; 400 × 220) takes the velocity from where
// the pad is hit: top = soft (1), bottom = hard (127), as FL's FPC. Mouse down = note on, mouse up = note off.
// Environment (for headless tests; every variable optional):
//   LYNX_TEST_PADS_AUTO = "call:key:velocity:length,..."  hits as if clicked: at the Nth process() call that has audio
//                         (counted per instance from 1), the key's note on goes out (velocity 1..127), and its note off
//                         `length` calls later. e.g. "40:36:100:4,80:38:64:4".
//   LYNX_TEST_PADS_ECHO = 1   every note the host sends in also goes out (a "MIDI thru" plugin): what a host must not
//                             record twice.
//   LYNX_TEST_PADS_LOG = path  appends "in on|off key offset" / "out on|off key velocity" lines.
#define INIT_CLASS_IID
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/gui/iplugview.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(_WIN32)
 #include <windows.h>
#endif

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace
{
const FUID padsUid (0x4C594E58, 0x50414453, 0x31393063, 0x6B735633); // "LYNXPADS190cksV3"
constexpr int padCount = 8, firstKey = 36;
const char* const padNames[padCount] = { "Kick", "Snare", "Clap", "Hat", "Tom 1", "Tom 2", "Ride", "Crash" };

void copyString (String128 to, const char* from)
{
    int i = 0;
    for (; from[i] != 0 && i < 127; ++i)
        to[i] = (char16) from[i];
    to[i] = 0;
}

// The window -> audio thread: a single-producer single-consumer ring of pad hits (key, velocity 0 = off)
struct HitRing
{
    static constexpr int size = 256;
    std::atomic<int> head { 0 }, tail { 0 };
    int keys[size] {};
    int velocities[size] {};
    bool push (int key, int velocity)
    {
        const auto h = head.load (std::memory_order_relaxed);
        const auto next = (h + 1) % size;
        if (next == tail.load (std::memory_order_acquire))
            return false;
        keys[h] = key;
        velocities[h] = velocity;
        head.store (next, std::memory_order_release);
        return true;
    }
    bool pop (int& key, int& velocity)
    {
        const auto t = tail.load (std::memory_order_relaxed);
        if (t == head.load (std::memory_order_acquire))
            return false;
        key = keys[t];
        velocity = velocities[t];
        tail.store ((t + 1) % size, std::memory_order_release);
        return true;
    }
};

class TestPads;

class PadsView final : public IPlugView
{
public:
    explicit PadsView (TestPads& p) : pads (p) {}
    tresult PLUGIN_API queryInterface (const TUID iid, void** obj) override
    {
        if (FUnknownPrivate::iidEqual (iid, FUnknown::iid) || FUnknownPrivate::iidEqual (iid, IPlugView::iid))
        {
            addRef();
            *obj = static_cast<IPlugView*> (this);
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
    tresult PLUGIN_API isPlatformTypeSupported (FIDString type) override
    {
#if defined(_WIN32)
        return std::strcmp (type, kPlatformTypeHWND) == 0 ? kResultTrue : kResultFalse;
#else
        (void) type;
        return kResultFalse;
#endif
    }
    tresult PLUGIN_API attached (void* parent, FIDString type) override;
    tresult PLUGIN_API removed() override;
    tresult PLUGIN_API onWheel (float) override { return kResultFalse; }
    tresult PLUGIN_API onKeyDown (char16, int16, int16) override { return kResultFalse; }
    tresult PLUGIN_API onKeyUp (char16, int16, int16) override { return kResultFalse; }
    tresult PLUGIN_API getSize (ViewRect* r) override
    {
        if (r == nullptr)
            return kInvalidArgument;
        *r = ViewRect (0, 0, width, height);
        return kResultOk;
    }
    tresult PLUGIN_API onSize (ViewRect*) override { return kResultOk; }
    tresult PLUGIN_API onFocus (TBool) override { return kResultOk; }
    tresult PLUGIN_API setFrame (IPlugFrame*) override { return kResultOk; }
    tresult PLUGIN_API canResize() override { return kResultFalse; }
    tresult PLUGIN_API checkSizeConstraint (ViewRect*) override { return kResultOk; }

    static constexpr int width = 400, height = 220;
    TestPads& pads;
    int held = -1; // the pad the mouse holds down
#if defined(_WIN32)
    HWND window = nullptr;
#endif

private:
    std::atomic<uint32> refs { 1 };
};

class TestPads final : public IComponent, public IAudioProcessor, public IEditController
{
public:
    TestPads() = default;
    virtual ~TestPads()
    {
        if (log != nullptr)
            std::fclose (log);
    }

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

    tresult PLUGIN_API initialize (FUnknown*) override
    {
        if (const char* e = std::getenv ("LYNX_TEST_PADS_ECHO"); e != nullptr && std::atoi (e) != 0)
            echo = true;
        if (const char* a = std::getenv ("LYNX_TEST_PADS_AUTO"); a != nullptr)
        {
            autoCount = 0;
            for (const char* p = a; *p != 0 && autoCount < maxAuto;)
            {
                int call = 0, key = 0, velocity = 0, length = 0, used = 0;
                if (std::sscanf (p, "%d:%d:%d:%d%n", &call, &key, &velocity, &length, &used) != 4)
                    break;
                autoHits[autoCount++] = { call, key, velocity, std::max (1, length) };
                p += used;
                while (*p == ',' || *p == ' ')
                    ++p;
            }
        }
        if (log == nullptr)
            if (const char* path = std::getenv ("LYNX_TEST_PADS_LOG"); path != nullptr && *path != 0)
                log = std::fopen (path, "a");
        return kResultOk;
    }
    tresult PLUGIN_API terminate() override { return kResultOk; }

    tresult PLUGIN_API getControllerClassId (TUID) override { return kNotImplemented; }
    tresult PLUGIN_API setIoMode (IoMode) override { return kResultOk; }
    int32 PLUGIN_API getBusCount (MediaType type, BusDirection dir) override { return type == kEvent || (type == kAudio && dir == kOutput) ? 1 : 0; }
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
        if (type == kEvent)
        {
            bus.channelCount = 16;
            copyString (bus.name, dir == kInput ? "MIDI In" : "MIDI Out");
            return kResultOk;
        }
        return kInvalidArgument; // (no audio input)
    }
    tresult PLUGIN_API getRoutingInfo (RoutingInfo&, RoutingInfo&) override { return kNotImplemented; }
    tresult PLUGIN_API activateBus (MediaType, BusDirection, int32, TBool) override { return kResultOk; }
    tresult PLUGIN_API setActive (TBool state) override
    {
        if (! state)
            for (auto& v : voices)
                v.level = 0.0;
        return kResultOk;
    }
    tresult PLUGIN_API setState (IBStream*) override { return kResultOk; }
    tresult PLUGIN_API getState (IBStream* s) override
    {
        if (s != nullptr)
        {
            int32 written = 0;
            const char tag[4] = { 'L', 'T', 'P', '1' };
            s->write ((void*) tag, 4, &written);
        }
        return kResultOk;
    }

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
        return kResultOk;
    }
    tresult PLUGIN_API setProcessing (TBool) override { return kResultOk; }
    uint32 PLUGIN_API getTailSamples() override { return kInfiniteTail; }

    tresult PLUGIN_API process (ProcessData& data) override
    {
        const auto n = data.numSamples;
        float* left = nullptr;
        float* right = nullptr;
        if (data.numOutputs > 0 && data.outputs[0].numChannels >= 2 && n > 0)
        {
            left = data.outputs[0].channelBuffers32[0];
            right = data.outputs[0].channelBuffers32[1];
            std::memset (left, 0, sizeof (float) * (size_t) n);
            std::memset (right, 0, sizeof (float) * (size_t) n);
        }
        auto* out = data.outputEvents;

        // the host's notes: play them (and, as a thru plugin, send them on)
        if (auto* in = data.inputEvents)
            for (int32 i = 0, count = in->getEventCount(); i < count; ++i)
            {
                Event e {};
                if (in->getEvent (i, e) != kResultOk)
                    continue;
                const auto on = e.type == Event::kNoteOnEvent && e.noteOn.velocity > 0.0f;
                const auto off = e.type == Event::kNoteOffEvent || (e.type == Event::kNoteOnEvent && e.noteOn.velocity <= 0.0f);
                if (! on && ! off)
                    continue;
                const int key = on ? e.noteOn.pitch : e.type == Event::kNoteOffEvent ? e.noteOff.pitch : e.noteOn.pitch;
                if (log != nullptr)
                    std::fprintf (log, "in %s %d %d\n", on ? "on" : "off", key, (int) e.sampleOffset);
                if (on)
                    strike (key, e.noteOn.velocity);
                if (echo && out != nullptr)
                    send (out, key, on ? e.noteOn.velocity : 0.0f, e.sampleOffset);
            }

        // the window's hits (and the scripted ones): played and sent out at the block's start
        int key = 0, velocity = 0;
        while (hits.pop (key, velocity))
            hit (out, key, velocity);
        if (n > 0)
        {
            ++calls;
            for (int i = 0; i < autoCount; ++i)
            {
                if (autoHits[i].call == calls)
                    hit (out, autoHits[i].key, autoHits[i].velocity);
                if (autoHits[i].call + autoHits[i].length == calls)
                    hit (out, autoHits[i].key, 0);
            }
        }

        for (int32 s = 0; s < n; ++s)
        {
            float v = 0.0f;
            for (auto& voice : voices)
                if (voice.level > 1.0e-4)
                {
                    v += (float) (voice.level * std::sin (voice.phase));
                    voice.phase += voice.step;
                    if (voice.phase > 6.283185307179586)
                        voice.phase -= 6.283185307179586;
                    voice.level *= voice.decay;
                }
            if (left != nullptr)
            {
                left[s] = v;
                right[s] = v;
            }
        }
        if (log != nullptr)
            std::fflush (log);
        return kResultOk;
    }

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
    IPlugView* PLUGIN_API createView (FIDString name) override
    {
#if defined(_WIN32)
        if (name != nullptr && std::strcmp (name, ViewType::kEditor) == 0)
            return new PadsView (*this);
#else
        (void) name;
#endif
        return nullptr;
    }

    // the window: a pad hit (velocity 1..127) or its release (0)
    void clicked (int key, int velocity) { hits.push (key, velocity); }

private:
    struct Voice
    {
        double phase = 0.0, step = 0.0, level = 0.0, decay = 0.9999;
    };

    void strike (int key, float velocity)
    {
        const auto pad = ((key - firstKey) % padCount + padCount) % padCount;
        auto& v = voices[pad];
        v.phase = 0.0;
        v.step = 6.283185307179586 * 110.0 * (pad + 1) / rate;
        v.level = 0.25 * (double) velocity;
        v.decay = std::pow (0.001, 1.0 / (0.25 * rate)); // -60 dB in 250 ms
    }

    void send (IEventList* out, int key, float velocity, int32 offset)
    {
        if (out == nullptr)
            return;
        Event e {};
        e.busIndex = 0;
        e.sampleOffset = offset;
        if (velocity > 0.0f)
        {
            e.type = Event::kNoteOnEvent;
            e.noteOn.channel = 9; // (MIDI channel 10, drums)
            e.noteOn.pitch = (int16) key;
            e.noteOn.velocity = velocity;
            e.noteOn.noteId = -1;
        }
        else
        {
            e.type = Event::kNoteOffEvent;
            e.noteOff.channel = 9;
            e.noteOff.pitch = (int16) key;
            e.noteOff.velocity = 0.0f;
            e.noteOff.noteId = -1;
        }
        out->addEvent (e);
        if (log != nullptr)
            std::fprintf (log, "out %s %d %d\n", velocity > 0.0f ? "on" : "off", key, (int) std::lround (velocity * 127.0f));
    }

    void hit (IEventList* out, int key, int velocity)
    {
        const auto v = (float) std::max (0, std::min (127, velocity)) / 127.0f;
        if (v > 0.0f)
            strike (key, v);
        send (out, key, v, 0);
    }

    struct AutoHit
    {
        int call = 0, key = 36, velocity = 100, length = 4;
    };
    static constexpr int maxAuto = 64;
    AutoHit autoHits[maxAuto];
    int autoCount = 0;
    long long calls = 0;
    bool echo = false;
    HitRing hits;
    Voice voices[padCount];
    double rate = 44100.0;
    std::FILE* log = nullptr;
    std::atomic<uint32> refs { 1 };
};

#if defined(_WIN32)
RECT padRect (int pad)
{
    const int w = PadsView::width / 4, h = (PadsView::height - 20) / 2;
    const int col = pad % 4, row = 1 - pad / 4; // (pad 1 at the bottom left, as FPC)
    return { col * w + 4, 20 + row * h + 4, (col + 1) * w - 4, 20 + (row + 1) * h - 4 };
}

LRESULT CALLBACK padsProc (HWND h, UINT m, WPARAM w, LPARAM l)
{
    auto* view = (PadsView*) GetWindowLongPtrA (h, GWLP_USERDATA);
    switch (m)
    {
        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            auto dc = BeginPaint (h, &ps);
            RECT all;
            GetClientRect (h, &all);
            auto bg = CreateSolidBrush (RGB (32, 34, 38));
            FillRect (dc, &all, bg);
            DeleteObject (bg);
            SetBkMode (dc, TRANSPARENT);
            SetTextColor (dc, RGB (200, 200, 200));
            RECT title { 6, 2, PadsView::width, 18 };
            DrawTextA (dc, "LYNX Test Pads - click: top soft, bottom hard", -1, &title, DT_LEFT | DT_SINGLELINE);
            for (int pad = 0; pad < padCount; ++pad)
            {
                auto r = padRect (pad);
                auto brush = CreateSolidBrush (view != nullptr && view->held == pad ? RGB (229, 56, 59) : RGB (70, 74, 82));
                FillRect (dc, &r, brush);
                DeleteObject (brush);
                char label[32];
                std::snprintf (label, sizeof (label), "%s (%d)", padNames[pad], firstKey + pad);
                DrawTextA (dc, label, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
            EndPaint (h, &ps);
            return 0;
        }
        case WM_LBUTTONDOWN:
        {
            const int x = (short) LOWORD (l), y = (short) HIWORD (l);
            for (int pad = 0; view != nullptr && pad < padCount; ++pad)
            {
                const auto r = padRect (pad);
                if (x >= r.left && x < r.right && y >= r.top && y < r.bottom)
                {
                    const auto velocity = 1 + (int) std::lround (126.0 * (double) (y - r.top) / (double) std::max (1L, r.bottom - r.top - 1));
                    view->held = pad;
                    view->pads.clicked (firstKey + pad, std::min (127, velocity));
                    SetCapture (h);
                    InvalidateRect (h, nullptr, FALSE);
                    break;
                }
            }
            return 0;
        }
        case WM_LBUTTONUP:
        case WM_CAPTURECHANGED:
            if (view != nullptr && view->held >= 0)
            {
                view->pads.clicked (firstKey + view->held, 0);
                view->held = -1;
                InvalidateRect (h, nullptr, FALSE);
            }
            if (m == WM_LBUTTONUP)
                ReleaseCapture();
            return 0;
        default:
            break;
    }
    return DefWindowProcA (h, m, w, l);
}

tresult PLUGIN_API PadsView::attached (void* parent, FIDString)
{
    static bool registered = false;
    if (! registered)
    {
        WNDCLASSA c {};
        c.lpfnWndProc = padsProc;
        c.hInstance = GetModuleHandleA (nullptr);
        c.hCursor = LoadCursor (nullptr, IDC_ARROW);
        c.lpszClassName = "LYNXTestPadsView";
        RegisterClassA (&c);
        registered = true;
    }
    window = CreateWindowExA (0, "LYNXTestPadsView", "LYNX Test Pads", WS_CHILD | WS_VISIBLE, 0, 0, width, height, (HWND) parent,
                              nullptr, GetModuleHandleA (nullptr), nullptr);
    if (window == nullptr)
        return kResultFalse;
    SetWindowLongPtrA (window, GWLP_USERDATA, (LONG_PTR) this);
    return kResultOk;
}

tresult PLUGIN_API PadsView::removed()
{
    if (window != nullptr)
    {
        SetWindowLongPtrA (window, GWLP_USERDATA, 0);
        DestroyWindow (window);
        window = nullptr;
    }
    return kResultOk;
}
#else
tresult PLUGIN_API PadsView::attached (void*, FIDString) { return kResultFalse; }
tresult PLUGIN_API PadsView::removed() { return kResultOk; }
#endif

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
        padsUid.toTUID (info->cid);
        info->cardinality = PClassInfo::kManyInstances;
        std::strcpy (info->category, kVstAudioEffectClass);
        std::strcpy (info->name, "LYNX Test Pads");
        return kResultOk;
    }
    tresult PLUGIN_API getClassInfo2 (int32 index, PClassInfo2* info) override
    {
        if (index != 0)
            return kInvalidArgument;
        std::memset (info, 0, sizeof (*info));
        padsUid.toTUID (info->cid);
        info->cardinality = PClassInfo::kManyInstances;
        std::strcpy (info->category, kVstAudioEffectClass);
        std::strcpy (info->name, "LYNX Test Pads");
        std::strcpy (info->subCategories, "Instrument|Drum");
        std::strcpy (info->vendor, "LYNX");
        std::strcpy (info->version, "1.0.0");
        std::strcpy (info->sdkVersion, "VST 3.7");
        return kResultOk;
    }
    tresult PLUGIN_API createInstance (FIDString cid, FIDString iid, void** obj) override
    {
        if (! FUnknownPrivate::iidEqual (cid, padsUid.toTUID()))
        {
            *obj = nullptr;
            return kNoInterface;
        }
        auto* pads = new TestPads();
        const auto result = pads->queryInterface (iid, obj);
        pads->release();
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
__attribute__ ((visibility ("default"))) IPluginFactory* PLUGIN_API GetPluginFactory() { return static_cast<IPluginFactory2*> (&factory); }
__attribute__ ((visibility ("default"))) bool ModuleEntry (void*) { return true; }
__attribute__ ((visibility ("default"))) bool ModuleExit() { return true; }
#endif
}
