/* A minimal multi-output VST2 instrument (round 188d, issue #114): tests the wrapper's output mapping (FL's Processing ›
 * Connections) in LYNX and in FL Studio without third-party plugins. Same binary interface as lynx_test_vst2.c:
 *   zig cc -shared -O2 -target x86_64-windows-gnu tools/testplugin/lynx_test_drums_vst2.c -o "LYNX Test Drums.dll"
 *   zig cc -shared -O2 -fPIC tools/testplugin/lynx_test_drums_vst2.c -o lynx_test_drums.so   (Linux)
 * 8 stereo outputs (16 channels) named Kick, Snare, Clap, Hat, Tom 1, Tom 2, Ride, Crash (effGetOutputProperties).
 * Key 36 + k plays a decaying sine at 110 × (k + 1) Hz on output k only, with k = (key − 36) mod 8 (C plays output 1,
 * C# output 2… in every octave).
 * No parameters, no editor, no state. Build options: -DPLUGIN_NAME="..." -DPLUGIN_ID=0x... (a second copy beside the
 * first), -DLAST_PAIR_INACTIVE (the Crash pins without kVstPinIsActive: which outputs a host starts active).
 */
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
 #define EXPORT __declspec (dllexport)
#else
 #define EXPORT __attribute__ ((visibility ("default")))
#endif

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
    char label[64];
    int32_t flags, arrangementType;
    char shortLabel[8];
    char future[48];
} PinProperties;

#ifndef PLUGIN_NAME
 #define PLUGIN_NAME "LYNX Test Drums"
#endif
#ifndef PLUGIN_ID
 #define PLUGIN_ID 0x4C584432 /* 'LXD2' */
#endif

enum { outputPairs = 8 };
static const char* const names[outputPairs] = { "Kick", "Snare", "Clap", "Hat", "Tom 1", "Tom 2", "Ride", "Crash" };

typedef struct
{
    Effect effect;
    float rate;
    float amplitude[outputPairs];
    double phase[outputPairs];
} Drums;

static intptr_t dispatch (Effect* e, int32_t op, int32_t index, intptr_t value, void* ptr, float opt)
{
    Drums* s = (Drums*) e->object;
    (void) value;
    switch (op)
    {
        case 1: free (s); return 0;                                  /* close */
        case 10: s->rate = opt; return 0;                            /* setSampleRate */
        case 25:                                                     /* processEvents */
        {
            Events* ev = (Events*) ptr;
            for (int i = 0; i < ev->numEvents; ++i)
            {
                const unsigned char* d = ev->events[i]->data;
                if ((d[0] & 0xF0) == 0x90 && d[2] > 0)
                {
                    const int k = ((d[1] - 36) % outputPairs + outputPairs) % outputPairs;
                    s->amplitude[k] = d[2] / 127.0f;
                    s->phase[k] = 0.0;
                }
            }
            return 1;
        }
        case 33:                                                     /* getInputProperties: none */
            return 0;
        case 34:                                                     /* getOutputProperties */
        {
            if (index < 0 || index >= 2 * outputPairs || ptr == 0)
                return 0;
            PinProperties* p = (PinProperties*) ptr;
            memset (p, 0, sizeof (*p));
            strcpy (p->label, names[index / 2]);
            strncpy (p->shortLabel, names[index / 2], 7);
            p->flags = 1 | ((index & 1) == 0 ? 2 : 0);              /* active; the left pin of a stereo pair */
#ifdef LAST_PAIR_INACTIVE
            if (index / 2 == outputPairs - 1)
                p->flags &= ~1;                                      /* the Crash pair not flagged active */
#endif
            p->arrangementType = 1;                                  /* stereo */
            return 1;
        }
        case 35: return 2;                                           /* getPlugCategory: synth */
        case 45: strcpy ((char*) ptr, PLUGIN_NAME); return 1;      /* getEffectName */
        case 47: strcpy ((char*) ptr, "LYNX"); return 1;             /* getVendorString */
        case 48: strcpy ((char*) ptr, PLUGIN_NAME); return 1;      /* getProductString */
        case 51:                                                     /* canDo */
            return ptr != 0 && (strcmp ((const char*) ptr, "receiveVstEvents") == 0 || strcmp ((const char*) ptr, "receiveVstMidiEvent") == 0) ? 1 : 0;
        case 58: return 2400;                                        /* getVstVersion */
        default: return 0;
    }
}

static void setParam (Effect* e, int32_t i, float v) { (void) e, (void) i, (void) v; }
static float getParam (Effect* e, int32_t i) { (void) e, (void) i; return 0.0f; }

static void processReplacing (Effect* e, float** in, float** out, int32_t n)
{
    Drums* s = (Drums*) e->object;
    const float rate = s->rate > 0 ? s->rate : 44100.0f;
    const float decay = expf (-1.0f / (0.25f * rate));              /* ~250 ms */
    (void) in;
    for (int k = 0; k < outputPairs; ++k)
    {
        const double step = 110.0 * (k + 1) / rate;
        for (int32_t i = 0; i < n; ++i)
        {
            const float v = s->amplitude[k] * 0.5f * (float) sin (2.0 * 3.14159265358979 * s->phase[k]);
            s->phase[k] += step;
            if (s->phase[k] >= 1.0) s->phase[k] -= 1.0;
            s->amplitude[k] *= decay;
            out[2 * k][i] = v;
            out[2 * k + 1][i] = v;
        }
    }
}

EXPORT Effect* VSTPluginMain (HostCallback host)
{
    Drums* s = (Drums*) calloc (1, sizeof (Drums));
    if (s == 0 || host (0, 1, 0, 0, 0, 0.0f) == 0)                 /* audioMasterVersion */
        return 0;
    s->rate = 44100.0f;
    s->effect.magic = 0x56737450;
    s->effect.dispatcher = dispatch;
    s->effect.setParameter = setParam;
    s->effect.getParameter = getParam;
    s->effect.processReplacing = processReplacing;
    s->effect.numPrograms = 1;
    s->effect.numParams = 0;
    s->effect.numInputs = 0;
    s->effect.numOutputs = 2 * outputPairs;
    s->effect.flags = (1 << 4) | (1 << 8);                          /* replacing, synth */
    s->effect.object = s;
    s->effect.uniqueId = PLUGIN_ID;
    s->effect.version = 1;
    return &s->effect;
}
