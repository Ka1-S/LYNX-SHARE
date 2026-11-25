/* An LV2 effect (a 0.5 gain) with an X11 UI that lists the UI resize extension in its .ttl but whose extension_data()
 * gives NULL for it, as DrumGizmo 0.9.20's UI does (round 190rf: crash reports 0.1.29 x5 / 0.1.30, "making the editor of
 * DrumGizmo": JUCE's LV2 host read the extension's struct through the null pointer). Linux:
 *   cc -shared -fPIC -O2 -I<JUCE>/modules/juce_audio_processors/format_types/LV2_SDK/lv2 tools/testplugin/lynx_test_lv2ui.c \
 *      -o tools/testplugin/lynx_test_lv2ui.lv2/lynx_test_lv2ui.so -ldl
 * then LYNX --plugwintest tools/testplugin/lynx_test_lv2ui.lv2 report.txt (LV2_PATH not needed: the bundle is named).
 * Its UI is a plain 240x120 X11 child window of the host's parent (Xlib looked up at run time).
 */
#include <lv2/core/lv2.h>
#include <lv2/ui/ui.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define PLUGIN_URI "urn:lynx:test-lv2ui"
#define UI_URI "urn:lynx:test-lv2ui#ui"

typedef struct { const float* in; float* out; } Gain;

static LV2_Handle instantiate (const LV2_Descriptor* d, double rate, const char* path, const LV2_Feature* const* f)
{
    (void) d; (void) rate; (void) path; (void) f;
    return calloc (1, sizeof (Gain));
}
static void connect (LV2_Handle h, uint32_t port, void* data)
{
    Gain* g = (Gain*) h;
    if (port == 0) g->in = (const float*) data;
    else g->out = (float*) data;
}
static void run (LV2_Handle h, uint32_t n)
{
    Gain* g = (Gain*) h;
    if (g->in == NULL || g->out == NULL) return;
    for (uint32_t i = 0; i < n; ++i) g->out[i] = 0.5f * g->in[i];
}
static void cleanup (LV2_Handle h) { free (h); }
static const void* plugin_extension (const char* uri) { (void) uri; return NULL; }

static const LV2_Descriptor descriptor = { PLUGIN_URI, instantiate, connect, NULL, run, NULL, cleanup, plugin_extension };
LV2_SYMBOL_EXPORT const LV2_Descriptor* lv2_descriptor (uint32_t index) { return index == 0 ? &descriptor : NULL; }

/* ---- the UI ---- */
typedef void* (*OpenDisplay) (const char*);
typedef unsigned long (*CreateSimpleWindow) (void*, unsigned long, int, int, unsigned, unsigned, unsigned, unsigned long, unsigned long);
typedef int (*MapWindow) (void*, unsigned long);
typedef int (*DestroyWindow) (void*, unsigned long);
typedef int (*CloseDisplay) (void*);
typedef int (*Flush) (void*);

typedef struct { void* xlib; void* display; unsigned long window; } Ui;

static LV2UI_Handle ui_instantiate (const LV2UI_Descriptor* d, const char* plugin, const char* path, LV2UI_Write_Function write,
                                    LV2UI_Controller controller, LV2UI_Widget* widget, const LV2_Feature* const* features)
{
    (void) d; (void) plugin; (void) path; (void) write; (void) controller;
    unsigned long parent = 0;
    for (int i = 0; features != NULL && features[i] != NULL; ++i)
        if (strcmp (features[i]->URI, LV2_UI__parent) == 0)
            parent = (unsigned long) (uintptr_t) features[i]->data;
    Ui* ui = (Ui*) calloc (1, sizeof (Ui));
    ui->xlib = dlopen ("libX11.so.6", RTLD_LAZY | RTLD_LOCAL);
    if (ui->xlib != NULL && parent != 0)
    {
        OpenDisplay open = (OpenDisplay) dlsym (ui->xlib, "XOpenDisplay");
        CreateSimpleWindow create = (CreateSimpleWindow) dlsym (ui->xlib, "XCreateSimpleWindow");
        MapWindow map = (MapWindow) dlsym (ui->xlib, "XMapWindow");
        Flush flush = (Flush) dlsym (ui->xlib, "XFlush");
        ui->display = open != NULL ? open (NULL) : NULL;
        if (ui->display != NULL && create != NULL && map != NULL)
        {
            ui->window = create (ui->display, parent, 0, 0, 240, 120, 0, 0, 0x808080);
            map (ui->display, ui->window);
            if (flush != NULL) flush (ui->display);
        }
    }
    *widget = (LV2UI_Widget) (uintptr_t) ui->window;
    return ui;
}
static void ui_cleanup (LV2UI_Handle h)
{
    Ui* ui = (Ui*) h;
    if (ui->display != NULL)
    {
        DestroyWindow destroy = (DestroyWindow) dlsym (ui->xlib, "XDestroyWindow");
        CloseDisplay close = (CloseDisplay) dlsym (ui->xlib, "XCloseDisplay");
        if (destroy != NULL && ui->window != 0) destroy (ui->display, ui->window);
        if (close != NULL) close (ui->display);
    }
    free (ui);
}
static void ui_port_event (LV2UI_Handle h, uint32_t port, uint32_t size, uint32_t format, const void* buffer)
{
    (void) h; (void) port; (void) size; (void) format; (void) buffer;
}
/* The .ttl lists ui:resize and ui:idleInterface as extension data; like DrumGizmo's UI, this one gives NULL for both */
static const void* ui_extension (const char* uri) { (void) uri; return NULL; }

static const LV2UI_Descriptor ui_descriptor = { UI_URI, ui_instantiate, ui_cleanup, ui_port_event, ui_extension };
LV2_SYMBOL_EXPORT const LV2UI_Descriptor* lv2ui_descriptor (uint32_t index) { return index == 0 ? &ui_descriptor : NULL; }
