/* A minimal VST2 effect with two input and two output pairs (round 188d): tests a host's effect connections (FL's
 * wrapper Processing › Connections: inputs as sidechain indexes, "Auto map inputs") without third-party plugins.
 *   zig cc -shared -O2 -target x86_64-windows-gnu tools/testplugin/lynx_test_sc_vst2.c -o "LYNX Test SC.dll"
 * Inputs "Main" (1-2) and "Side" (3-4); outputs "Main" (1-2) = main input, "Side" (3-4) = side input (so a host can
 * hear where each input comes from). No parameters, no editor, no state.
 */
#ifdef _WIN32
 #include <windows.h>
#endif
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
    char label[64];
    int32_t flags, arrangementType;
    char shortLabel[8];
    char future[48];
} PinProperties;

#ifndef PLUGIN_NAME
 #define PLUGIN_NAME "LYNX Test SC"
#endif
#ifndef PLUGIN_ID
 #define PLUGIN_ID 0x4C585343 /* 'LXSC' */
#endif

static intptr_t dispatch (Effect* e, int32_t op, int32_t index, intptr_t value, void* ptr, float opt)
{
    (void) value, (void) opt;
    switch (op)
    {
        case 1: free (e); return 0;                                    /* close */
#ifdef _WIN32
        case 13:                                                       /* editGetRect: a plain 240x80 window (FL */
        {                                                              /* opens no wrapper for an effect without) */
            static short rect[4] = { 0, 0, 80, 240 };
            *(short**) ptr = rect;
            return 1;
        }
        case 14:                                                       /* editOpen */
            e->user = CreateWindowExA (0, "STATIC", PLUGIN_NAME, WS_CHILD | WS_VISIBLE | SS_CENTER, 0, 0, 240, 80, (HWND) ptr, 0,
                                       GetModuleHandleA (0), 0);
            return e->user != 0;
        case 15:                                                       /* editClose */
            if (e->user != 0) DestroyWindow ((HWND) e->user);
            e->user = 0;
            return 0;
#endif
        case 33:                                                       /* getInputProperties */
        case 34:                                                       /* getOutputProperties */
        {
            if (index < 0 || index >= 4 || ptr == 0)
                return 0;
            PinProperties* p = (PinProperties*) ptr;
            memset (p, 0, sizeof (*p));
            strcpy (p->label, index < 2 ? "Main" : "Side");
            strcpy (p->shortLabel, index < 2 ? "Main" : "Side");
            p->flags = 1 | ((index & 1) == 0 ? 2 : 0);                /* active; the left pin of a stereo pair */
            p->arrangementType = 1;
            return 1;
        }
        case 35: return 1;                                             /* getPlugCategory: effect */
        case 45: strcpy ((char*) ptr, PLUGIN_NAME); return 1;         /* getEffectName */
        case 47: strcpy ((char*) ptr, "LYNX"); return 1;               /* getVendorString */
        case 48: strcpy ((char*) ptr, PLUGIN_NAME); return 1;         /* getProductString */
        case 58: return 2400;                                          /* getVstVersion */
        default: return 0;
    }
}

static void setParam (Effect* e, int32_t i, float v) { (void) e, (void) i, (void) v; }
static float getParam (Effect* e, int32_t i) { (void) e, (void) i; return 0.0f; }

static void processReplacing (Effect* e, float** in, float** out, int32_t n)
{
    (void) e;
    for (int c = 0; c < 4; ++c)
        if (out[c] != in[c])
            memcpy (out[c], in[c], sizeof (float) * (size_t) n);
}

EXPORT Effect* VSTPluginMain (HostCallback host)
{
    Effect* e = (Effect*) calloc (1, sizeof (Effect));
    if (e == 0 || host (0, 1, 0, 0, 0, 0.0f) == 0)
        return 0;
    e->magic = 0x56737450;
    e->dispatcher = dispatch;
    e->setParameter = setParam;
    e->getParameter = getParam;
    e->processReplacing = processReplacing;
    e->numPrograms = 1;
    e->numInputs = 4;
    e->numOutputs = 4;
    e->flags = 1 << 4;                                                 /* replacing */
#ifdef _WIN32
    e->flags |= 1;                                                     /* has editor */
#endif
    e->object = e;
    e->uniqueId = PLUGIN_ID;
    e->version = 1;
    return e;
}
