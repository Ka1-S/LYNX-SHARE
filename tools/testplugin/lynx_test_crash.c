/* A CLAP effect that crashes on purpose, for testing LYNX's crash protection (round 111; Linux / macOS: round 190dt):
 *   Windows: zig cc -shared -O2 -target x86_64-windows-gnu -Ithird_party/clap/include tools/testplugin/lynx_test_crash.c -luser32 -lgdi32 \
 *                -o "LYNX Test Crash.clap"
 *   Linux / macOS: cc -shared -fPIC -O2 -Ithird_party/clap/include tools/testplugin/lynx_test_crash.c -o "LYNX Test Crash.clap"
 * Otherwise a 0.5 gain (in -> out), with a 4-byte state ("LXC1").
 * Environment variable LYNX_TEST_CRASH:
 *   "process" - a null-pointer write (access violation / SIGSEGV) in the Nth process() call, N = LYNX_TEST_CRASH_AFTER
 *               (default 20); counted per loaded module, so an instance made again later does not crash again
 *               (LYNX --plugintest / --crashguardtest: the slot must be switched off, the render must finish)
 *   "memcpy"  - the same, but as a memcpy from a null pointer (the fault is inside the C library, issue #155)
 *   "state"   - a null-pointer write when a saved state is loaded into it (state extension load)
 *   "thread"  - (Linux / macOS; round 190gg) in the Nth process() call it starts a thread of its own that writes to a
 *               null pointer: a fault outside every call the host makes (LYNX --crashguardtest ... thread)
 *   "editor-create" - (Linux / macOS; round 190gg) it offers an editor (CLAP gui, X11 / Cocoa) and writes to a null
 *               pointer when the host creates it (LYNX --crashguardtest ... ui: the editor is made under the guard)
 *   "exit"    - (Windows) a fail-fast (0xC0000409) when the DLL is unloaded at process exit, like the Waves shell whose
 *               teardown crashed LYNX's scan child (LYNX.exe --scantest: the plugin must be found, not blacklisted)
 *   "editor"  - (Windows; issue #32, round 174a) an access violation in its own editor window's procedure, on the
 *               window's first WM_TIMER (~0.3 s after it opens), like the synth editor that took LYNX down
 *               (LYNX.exe --editorcrashtest: the editor must be closed, LYNX must keep running)
 * Its editor (CLAP gui, Win32 child window; Windows only) is a plain grey 240x120 window.
 */
#if defined(_WIN32)
 #include <windows.h>
#else
 #include <pthread.h>
#endif
#include <clap/clap.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const char* const features[] = { CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY, NULL };

static const clap_plugin_descriptor_t descriptor = {
    CLAP_VERSION_INIT, "org.lynx.test-crash", "LYNX Test Crash", "LYNX", "https://github.com/Ka1-S/LYNX-SHARE", "", "", "1.0.0",
    "Crashes on purpose (crash protection tests)", features
};

static uint32_t ports_count (const clap_plugin_t* p, bool in) { (void) p; (void) in; return 1; }
static bool ports_get (const clap_plugin_t* p, uint32_t index, bool in, clap_audio_port_info_t* info)
{
    (void) p;
    if (index != 0) return false;
    memset (info, 0, sizeof (*info));
    info->id = in ? 0 : 1;
    strcpy (info->name, in ? "In" : "Out");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = 2;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}
static const clap_plugin_audio_ports_t audio_ports = { ports_count, ports_get };

static bool p_init (const clap_plugin_t* p) { (void) p; return true; }
static void p_destroy (const clap_plugin_t* p) { free ((void*) p); }
static bool p_activate (const clap_plugin_t* p, double r, uint32_t a, uint32_t b) { (void) p; (void) r; (void) a; (void) b; return true; }
static void p_deactivate (const clap_plugin_t* p) { (void) p; }
static bool p_start (const clap_plugin_t* p) { (void) p; return true; }
static void p_stop (const clap_plugin_t* p) { (void) p; }
static void p_reset (const clap_plugin_t* p) { (void) p; }
static int crash_mode (const char* what)
{
#if defined(_WIN32)
    char value[32] = { 0 };
    return GetEnvironmentVariableA ("LYNX_TEST_CRASH", value, sizeof (value)) > 0 && strcmp (value, what) == 0;
#else
    const char* value = getenv ("LYNX_TEST_CRASH");
    return value != NULL && strcmp (value, what) == 0;
#endif
}
static int crash_after (void)
{
#if defined(_WIN32)
    char value[32] = { 0 };
    const int n = GetEnvironmentVariableA ("LYNX_TEST_CRASH_AFTER", value, sizeof (value)) > 0 ? atoi (value) : 0;
#else
    const char* value = getenv ("LYNX_TEST_CRASH_AFTER");
    const int n = value != NULL ? atoi (value) : 0;
#endif
    return n > 0 ? n : 20;
}
static volatile int sink = 0;
static void null_write (void)
{
    volatile uintptr_t address = 0; /* (opaque: a compiler may drop a write to a constant null) */
    *(volatile int*) address = 1;
}
static void null_copy (void)
{
    char to[64];
    const char* volatile from = NULL;
    volatile size_t size = sizeof (to); /* (a real call into the C library) */
    memcpy (to, (const char*) from, size);
    sink = to[0];
}
#if ! defined(_WIN32)
static void* faulting_thread (void* arg)
{
    (void) arg;
    null_write();
    return NULL;
}
#endif
static clap_process_status p_process (const clap_plugin_t* p, const clap_process_t* proc)
{
    static int calls = 0;
    (void) p;
    if (++calls == crash_after())
    {
        if (crash_mode ("process"))
            null_write();
        if (crash_mode ("memcpy"))
            null_copy();
#if ! defined(_WIN32)
        if (crash_mode ("thread"))
        {
            pthread_t thread;
            if (pthread_create (&thread, NULL, faulting_thread, NULL) == 0)
                pthread_detach (thread);
        }
#endif
    }
    if (proc->audio_inputs_count < 1 || proc->audio_outputs_count < 1) return CLAP_PROCESS_CONTINUE;
    for (uint32_t c = 0; c < 2 && c < proc->audio_inputs[0].channel_count && c < proc->audio_outputs[0].channel_count; ++c)
        for (uint32_t i = 0; i < proc->frames_count; ++i)
            proc->audio_outputs[0].data32[c][i] = 0.5f * proc->audio_inputs[0].data32[c][i];
    return CLAP_PROCESS_CONTINUE;
}
#if defined(_WIN32)
/* the editor: one child window; "editor" mode crashes in its window procedure */
static HWND editor_window = NULL;
static LRESULT CALLBACK editor_proc (HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_TIMER && crash_mode ("editor"))
        *(volatile int*) (uintptr_t) 0 = 1;
    if (m == WM_PAINT)
    {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint (h, &ps);
        FillRect (dc, &ps.rcPaint, (HBRUSH) GetStockObject (GRAY_BRUSH));
        EndPaint (h, &ps);
        return 0;
    }
    return DefWindowProcA (h, m, w, l);
}
static bool g_api (const clap_plugin_t* p, const char* api, bool floating) { (void) p; return ! floating && strcmp (api, CLAP_WINDOW_API_WIN32) == 0; }
static bool g_preferred (const clap_plugin_t* p, const char** api, bool* floating) { (void) p; *api = CLAP_WINDOW_API_WIN32; *floating = false; return true; }
static bool g_create (const clap_plugin_t* p, const char* api, bool floating) { (void) p; return g_api (p, api, floating); }
static void g_destroy (const clap_plugin_t* p)
{
    (void) p;
    if (editor_window != NULL) DestroyWindow (editor_window);
    editor_window = NULL;
}
static bool g_scale (const clap_plugin_t* p, double s) { (void) p; (void) s; return true; }
static bool g_size (const clap_plugin_t* p, uint32_t* w, uint32_t* h) { (void) p; *w = 240; *h = 120; return true; }
static bool g_can_resize (const clap_plugin_t* p) { (void) p; return false; }
static bool g_hints (const clap_plugin_t* p, clap_gui_resize_hints_t* h) { (void) p; (void) h; return false; }
static bool g_adjust (const clap_plugin_t* p, uint32_t* w, uint32_t* h) { (void) p; *w = 240; *h = 120; return true; }
static bool g_set_size (const clap_plugin_t* p, uint32_t w, uint32_t h) { (void) p; (void) w; (void) h; return true; }
static bool g_parent (const clap_plugin_t* p, const clap_window_t* window)
{
    static int registered = 0;
    (void) p;
    if (! registered)
    {
        WNDCLASSA c;
        memset (&c, 0, sizeof (c));
        c.lpfnWndProc = editor_proc;
        c.hInstance = GetModuleHandleA (NULL);
        c.lpszClassName = "LynxTestCrashEditor";
        RegisterClassA (&c);
        registered = 1;
    }
    editor_window = CreateWindowExA (0, "LynxTestCrashEditor", "", WS_CHILD | WS_VISIBLE, 0, 0, 240, 120, (HWND) window->win32, NULL,
                                     GetModuleHandleA (NULL), NULL);
    if (editor_window != NULL)
        SetTimer (editor_window, 1, 300, NULL);
    return editor_window != NULL;
}
static bool g_transient (const clap_plugin_t* p, const clap_window_t* w) { (void) p; (void) w; return false; }
static void g_title (const clap_plugin_t* p, const char* t) { (void) p; (void) t; }
static bool g_show (const clap_plugin_t* p) { (void) p; if (editor_window != NULL) ShowWindow (editor_window, SW_SHOW); return true; }
static bool g_hide (const clap_plugin_t* p) { (void) p; if (editor_window != NULL) ShowWindow (editor_window, SW_HIDE); return true; }
static const clap_plugin_gui_t gui = { g_api, g_preferred, g_create, g_destroy, g_scale, g_size, g_can_resize, g_hints, g_adjust,
                                       g_set_size, g_parent, g_transient, g_title, g_show, g_hide };

#else
/* Linux / macOS: an editor only in "editor-create" mode, which crashes as the host creates it */
static bool g_api (const clap_plugin_t* p, const char* api, bool floating)
{
    (void) p;
    return ! floating && (strcmp (api, CLAP_WINDOW_API_X11) == 0 || strcmp (api, CLAP_WINDOW_API_COCOA) == 0);
}
static bool g_preferred (const clap_plugin_t* p, const char** api, bool* floating)
{
    (void) p;
#if defined(__APPLE__)
    *api = CLAP_WINDOW_API_COCOA;
#else
    *api = CLAP_WINDOW_API_X11;
#endif
    *floating = false;
    return true;
}
static bool g_create (const clap_plugin_t* p, const char* api, bool floating)
{
    if (crash_mode ("editor-create"))
        null_write();
    return g_api (p, api, floating);
}
static void g_destroy (const clap_plugin_t* p) { (void) p; }
static bool g_scale (const clap_plugin_t* p, double s) { (void) p; (void) s; return true; }
static bool g_size (const clap_plugin_t* p, uint32_t* w, uint32_t* h) { (void) p; *w = 240; *h = 120; return true; }
static bool g_can_resize (const clap_plugin_t* p) { (void) p; return false; }
static bool g_hints (const clap_plugin_t* p, clap_gui_resize_hints_t* h) { (void) p; (void) h; return false; }
static bool g_adjust (const clap_plugin_t* p, uint32_t* w, uint32_t* h) { (void) p; *w = 240; *h = 120; return true; }
static bool g_set_size (const clap_plugin_t* p, uint32_t w, uint32_t h) { (void) p; (void) w; (void) h; return true; }
static bool g_parent (const clap_plugin_t* p, const clap_window_t* window) { (void) p; (void) window; return true; }
static bool g_transient (const clap_plugin_t* p, const clap_window_t* w) { (void) p; (void) w; return false; }
static void g_title (const clap_plugin_t* p, const char* t) { (void) p; (void) t; }
static bool g_show (const clap_plugin_t* p) { (void) p; return true; }
static bool g_hide (const clap_plugin_t* p) { (void) p; return true; }
static const clap_plugin_gui_t gui = { g_api, g_preferred, g_create, g_destroy, g_scale, g_size, g_can_resize, g_hints, g_adjust,
                                       g_set_size, g_parent, g_transient, g_title, g_show, g_hide };
#endif

/* state: 4 bytes; "state" mode crashes when one is loaded */
static bool s_save (const clap_plugin_t* p, const clap_ostream_t* stream)
{
    (void) p;
    return stream->write (stream, "LXC1", 4) == 4;
}
static bool s_load (const clap_plugin_t* p, const clap_istream_t* stream)
{
    char data[4];
    (void) p;
    if (crash_mode ("state"))
        null_write();
    return stream->read (stream, data, 4) == 4;
}
static const clap_plugin_state_t state_ext = { s_save, s_load };

static const void* p_extension (const clap_plugin_t* p, const char* id)
{
    (void) p;
#if defined(_WIN32)
    if (strcmp (id, CLAP_EXT_GUI) == 0)
        return &gui;
#else
    if (strcmp (id, CLAP_EXT_GUI) == 0 && crash_mode ("editor-create"))
        return &gui;
#endif
    if (strcmp (id, CLAP_EXT_STATE) == 0)
        return &state_ext;
    return strcmp (id, CLAP_EXT_AUDIO_PORTS) == 0 ? &audio_ports : NULL;
}
static void p_main_thread (const clap_plugin_t* p) { (void) p; }

static uint32_t f_count (const clap_plugin_factory_t* f) { (void) f; return 1; }
static const clap_plugin_descriptor_t* f_descriptor (const clap_plugin_factory_t* f, uint32_t i) { (void) f; return i == 0 ? &descriptor : NULL; }
static const clap_plugin_t* f_create (const clap_plugin_factory_t* f, const clap_host_t* host, const char* id)
{
    (void) f; (void) host;
    if (strcmp (id, descriptor.id) != 0) return NULL;
    clap_plugin_t* p = (clap_plugin_t*) calloc (1, sizeof (clap_plugin_t));
    p->desc = &descriptor;
    p->init = p_init; p->destroy = p_destroy; p->activate = p_activate; p->deactivate = p_deactivate;
    p->start_processing = p_start; p->stop_processing = p_stop; p->reset = p_reset; p->process = p_process;
    p->get_extension = p_extension; p->on_main_thread = p_main_thread;
    return p;
}
static const clap_plugin_factory_t factory = { f_count, f_descriptor, f_create };

static bool e_init (const char* path) { (void) path; return true; }
static void e_deinit (void) {}
static const void* e_factory (const char* id) { return strcmp (id, CLAP_PLUGIN_FACTORY_ID) == 0 ? &factory : NULL; }

CLAP_EXPORT const clap_plugin_entry_t clap_entry = { CLAP_VERSION_INIT, e_init, e_deinit, e_factory };

#if defined(_WIN32)
BOOL WINAPI DllMain (HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void) instance;
    if (reason == DLL_PROCESS_DETACH && reserved != NULL && crash_mode ("exit")) /* the process is exiting */
        __fastfail (7);
    return TRUE;
}
#endif
