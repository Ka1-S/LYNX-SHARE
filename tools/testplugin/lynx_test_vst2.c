/* A minimal VST2 instrument for testing LYNX's own VST2 host (src/engine/Vst2Format.cpp) without third-party
 * plugins. Written against the VST 2.4 binary interface as LYNX declares it (no Steinberg headers):
 *   zig cc -shared -O2 -target x86_64-windows-gnu tools/testplugin/lynx_test_vst2.c -o "LYNX Test Synth.dll"
 *   LYNX.exe --vst2test "LYNX Test Synth.dll" report.txt
 * 32-bit build (round 176a, the 32-bit bridge LYNX-bridge32.exe):
 *   zig cc -shared -O2 -target x86-windows-gnu tools/testplugin/lynx_test_vst2.c -luser32 -o "LYNX Test Synth 32.dll"
 *   LYNX.exe --bridge32test "LYNX Test Synth 32.dll" report.txt
 * The wrapper's MIDI options on a running plugin (round 181c):
 *   LYNX_TEST_VST2_MIDILOG=midi.txt LYNX.exe --wrappermiditest "LYNX Test Synth.dll" report.txt
 * Sound: a sine at the played note, amplitude = parameter 0 ("Gain") × velocity.
 * Parameter 1 ("Tempo") reports the host tempo / 1000 it last read through audioMasterGetTime.
 * State: a program chunk ('LXV2' + gain + last tempo/1000), so a host must restore it with effSetChunk.
 * Parameter 2 ("Bits"): the plugin's pointer size / 64 (0.5 in a 32-bit build): which process it runs in.
 * Editor (Windows): a plain 240x120 child window. Environment (inherited by a bridge process):
 *   LYNX_TEST_VST2_CRASH=process  an access violation in the 20th processReplacing call (crash isolation tests)
 *   LYNX_TEST_VST2_LATENCY=<n>    reports n samples of latency (initialDelay)
 */
#ifdef _WIN32
 #include <windows.h>
#endif
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Effect Effect;
typedef intptr_t (*HostCallback) (Effect*, int32_t, int32_t, intptr_t, void*, float);

struct Effect
{
    int32_t magic;
    intptr_t (*dispatcher) (Effect*, int32_t, int32_t, intptr_t, void*, float);
    void (*process) (Effect*, float**, float**, int32_t);
    void (*setParameter) (Effect*, int32_t, float);
    float (*getParameter) (Effect*, int32_t);
    int32_t numPrograms, numParams, numInputs, numOutputs, flags;
    intptr_t resvd1, resvd2;
    int32_t initialDelay, realQualities, offQualities;
    float ioRatio;
    void* object;
    void* user;
    int32_t uniqueId, version;
    void (*processReplacing) (Effect*, float**, float**, int32_t);
    void* processDoubleReplacing;
    char future[56];
};

typedef struct
{
    int32_t type, byteSize, deltaFrames, flags, noteLength, noteOffset;
    unsigned char data[4];
    char detune, noteOffVelocity, r1, r2;
} MidiEvent;

typedef struct
{
    int32_t numEvents;
    intptr_t reserved;
    MidiEvent* events[2];
} Events;

typedef struct
{
    double samplePos, sampleRate, nanoSeconds, ppqPos, tempo, barStartPos, cycleStartPos, cycleEndPos;
    int32_t num, den, smpteOffset, smpteFrameRate, samplesToNextClock, flags;
} TimeInfo;

typedef struct
{
    Effect effect;
    HostCallback host;
    float gain, tempo, rate;
    int note;
    float amplitude;
    double phase;
    unsigned char chunk[12];
    int blocks, crash;
    void* window;
} Synth;

#ifdef _WIN32
/* LYNX_TEST_VST2_KEYS=eat|pass: the view takes the keyboard focus when clicked and either consumes every key or passes it
   back to its parent, as plugins do (issue #96). LYNX_TEST_VST2_KEYLOG: a file getting one line per key it saw. */
static LRESULT CALLBACK keysViewProc (HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_LBUTTONDOWN)
    {
        SetFocus (h);
        return 0;
    }
    if (m == WM_KEYDOWN || m == WM_KEYUP)
    {
        const char* path = getenv ("LYNX_TEST_VST2_KEYLOG");
        const char* mode = getenv ("LYNX_TEST_VST2_KEYS");
        FILE* f = path != 0 ? fopen (path, "a") : 0;
        if (f != 0)
        {
            fprintf (f, "%s %d %s\n", m == WM_KEYDOWN ? "down" : "up", (int) w, mode != 0 ? mode : "");
            fclose (f);
        }
        if (mode != 0 && strcmp (mode, "pass") == 0)
            return SendMessageA (GetParent (h), m, w, l);
        return 0;
    }
    return DefWindowProcA (h, m, w, l);
}

static HWND createKeysView (HWND parent)
{
    static int registered = 0;
    if (! registered)
    {
        WNDCLASSA c;
        memset (&c, 0, sizeof (c));
        c.lpfnWndProc = keysViewProc;
        c.hInstance = GetModuleHandleA (0);
        c.hbrBackground = (HBRUSH) (COLOR_BTNFACE + 1);
        c.lpszClassName = "LYNXTestKeysView";
        RegisterClassA (&c);
        registered = 1;
    }
    return CreateWindowExA (0, "LYNXTestKeysView", "LYNX Test Synth", WS_CHILD | WS_VISIBLE, 0, 0, 240, 120, parent, 0, GetModuleHandleA (0), 0);
}
#endif

static intptr_t dispatch (Effect* e, int32_t op, int32_t index, intptr_t value, void* ptr, float opt)
{
    Synth* s = (Synth*) e->object;
    (void) index;
    switch (op)
    {
        case 1: free (s); return 0;                                 /* close */
        case 10: s->rate = opt; return 0;                           /* setSampleRate */
        case 8: strcpy ((char*) ptr, index == 0 ? "Gain" : index == 1 ? "Tempo" : "Bits"); return 0; /* getParamName */
#ifdef _WIN32
        case 13:                                                    /* editGetRect */
        {
            static short rect[4] = { 0, 0, 120, 240 };              /* top, left, bottom, right */
            *(short**) ptr = rect;
            return 1;
        }
        case 14:                                                    /* editOpen: a child of the host's window */
            if (getenv ("LYNX_TEST_VST2_KEYS") != 0)
            {
                s->window = createKeysView ((HWND) ptr);
                return s->window != 0;
            }
            s->window = CreateWindowExA (0, "STATIC", "LYNX Test Synth", WS_CHILD | WS_VISIBLE | SS_CENTER, 0, 0, 240, 120,
                                         (HWND) ptr, 0, GetModuleHandleA (0), 0);
            return s->window != 0;
        case 15:                                                    /* editClose */
            if (s->window != 0) DestroyWindow ((HWND) s->window);
            s->window = 0;
            return 0;
#endif
        case 23:                                                    /* getChunk */
            memcpy (s->chunk, "LXV2", 4);
            memcpy (s->chunk + 4, &s->gain, 4);
            memcpy (s->chunk + 8, &s->tempo, 4); /* what the host's time info said (for the host test) */
            *(void**) ptr = s->chunk;
            return 12;
        case 24:                                                    /* setChunk */
            if (value >= 8 && memcmp (ptr, "LXV2", 4) == 0)
                memcpy (&s->gain, (char*) ptr + 4, 4);
            return 1;
        case 25:                                                    /* processEvents */
        {
            Events* ev = (Events*) ptr;
            for (int i = 0; i < ev->numEvents; ++i)
            {
                const unsigned char* d = ev->events[i]->data;
                { /* LYNX_TEST_VST2_MIDILOG = a file: every MIDI event as the host sent it (--wrappermiditest) */
                    const char* path = getenv ("LYNX_TEST_VST2_MIDILOG");
                    FILE* log = path != 0 ? fopen (path, "a") : 0;
                    if (log != 0)
                    {
                        fprintf (log, "%p %d %d %d\n", (void*) s, d[0], d[1], d[2]);
                        fclose (log);
                    }
                }
                if ((d[0] & 0xF0) == 0x90 && d[2] > 0) { s->note = d[1]; s->amplitude = d[2] / 127.0f; }
                else if ((d[0] & 0xF0) == 0x80 || ((d[0] & 0xF0) == 0x90 && d[2] == 0)) { if (d[1] == s->note) s->amplitude = 0.0f; }
            }
            return 1;
        }
        case 35: return 2;                                          /* getPlugCategory: synth */
        case 45: strcpy ((char*) ptr, "LYNX Test Synth"); return 1; /* getEffectName */
        case 47: strcpy ((char*) ptr, "LYNX"); return 1;            /* getVendorString */
        case 58: return 2400;                                       /* getVstVersion */
        default: return 0;
    }
}

static void setParam (Effect* e, int32_t i, float v)
{
    Synth* s = (Synth*) e->object;
    if (i == 0) s->gain = v;
}

static float getParam (Effect* e, int32_t i)
{
    Synth* s = (Synth*) e->object;
    return i == 0 ? s->gain : i == 1 ? s->tempo : (float) (sizeof (void*) * 8) / 64.0f;
}

static void processReplacing (Effect* e, float** in, float** out, int32_t n)
{
    Synth* s = (Synth*) e->object;
    (void) in;
    if (s->crash && ++s->blocks == 20)
        *(volatile int*) 0 = 1;                                     /* LYNX_TEST_VST2_CRASH=process */
    const TimeInfo* t = (const TimeInfo*) s->host (e, 7, 0, 0, 0, 0.0f);
    if (t != 0 && (t->flags & (1 << 10)))
        s->tempo = (float) (t->tempo / 1000.0);
    const double step = 440.0 * pow (2.0, (s->note - 69) / 12.0) / (s->rate > 0 ? s->rate : 44100.0);
    for (int32_t i = 0; i < n; ++i)
    {
        const float v = s->gain * s->amplitude * (float) sin (2.0 * 3.14159265358979 * s->phase);
        s->phase += step;
        if (s->phase >= 1.0) s->phase -= 1.0;
        out[0][i] = v;
        out[1][i] = v;
    }
}

__declspec (dllexport) Effect* VSTPluginMain (HostCallback host)
{
    Synth* s = (Synth*) calloc (1, sizeof (Synth));
    if (s == 0 || host (0, 1, 0, 0, 0, 0.0f) == 0) /* audioMasterVersion */
        return 0;
    s->host = host;
    s->gain = 0.5f;
    s->rate = 44100.0f;
    s->note = 69;
    s->effect.magic = 0x56737450;
    s->effect.dispatcher = dispatch;
    s->effect.setParameter = setParam;
    s->effect.getParameter = getParam;
    s->effect.processReplacing = processReplacing;
    s->effect.numPrograms = 1;
    s->effect.numParams = 3;
    s->effect.numInputs = 0;
    s->effect.numOutputs = 2;
    s->effect.flags = (1 << 4) | (1 << 5) | (1 << 8); /* replacing, program chunks, synth */
#ifdef _WIN32
    s->effect.flags |= 1;                               /* has editor */
#endif
    {
        const char* crash = getenv ("LYNX_TEST_VST2_CRASH");
        const char* latency = getenv ("LYNX_TEST_VST2_LATENCY");
        s->crash = crash != 0 && strcmp (crash, "process") == 0;
        s->effect.initialDelay = latency != 0 ? atoi (latency) : 0;
    }
    s->effect.object = s;
    s->effect.uniqueId = 0x4C585632;                    /* 'LXV2' */
    s->effect.version = 1;
    return &s->effect;
}
