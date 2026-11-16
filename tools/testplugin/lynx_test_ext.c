/* A CLAP effect for LYNX's CLAP extension and timing tests (round 190jg, BUILD-LIST 18):
 *   Windows: zig cc -shared -O2 -target x86_64-windows-gnu -Ithird_party/clap/include tools/testplugin/lynx_test_ext.c \
 *                -o "LYNX Test Ext.clap"
 *   Linux / macOS: cc -shared -fPIC -O2 -Ithird_party/clap/include tools/testplugin/lynx_test_ext.c -lpthread -o "LYNX Test Ext.clap"
 *   then: LYNX --clapext "LYNX Test Ext.clap" report.txt;  LYNX --automationtimingtest "LYNX Test Ext.clap" report.txt
 * Sound: it ignores its input and outputs its "Level" parameter (id 7, 0..1) as a constant (DC) on both channels,
 * taking each CLAP_EVENT_PARAM_VALUE at its own sample (the event's time): a step of the automation shows in the output
 * at the sample the host put it.
 * It also uses / offers, and writes down in the file named by the environment variable LYNX_TEST_EXT_LOG:
 *   thread-pool    on its 10th process call it asks the host to run 8 tasks ("pool ok=1 tasks=8 threads=<n>")
 *   tail           4800 samples;  voice-info: 3 voices of 16, overlapping notes
 *   render         the mode the host sets ("render offline" / "render realtime")
 *   state-context  a state saved for a preset / a duplicate / the project ("save context=<n>", "load context=<n>")
 *   remote-controls one page "Test / Main": Level, Width, then empty knobs
 *   preset-discovery + preset-load: three presets inside the plugin ("Low" 0.25, "Mid" 0.5, "High" 0.75)
 * and the parameter events it received ("P <time> <value> n=<frames>", the first 400).
 */
#include <clap/clap.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
 #include <windows.h>
#else
 #include <pthread.h>
#endif

#define LEVEL_ID 7
#define WIDTH_ID 9

static const char* const features[] = { CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY, NULL };
static const clap_plugin_descriptor_t descriptor = {
    CLAP_VERSION_INIT, "org.lynx.test-ext", "LYNX Test Ext", "LYNX", "https://github.com/Ka1-S/LYNX-SHARE", "", "", "1.0.0",
    "CLAP extension and timing tests", features
};

static FILE* log_file (void)
{
    static FILE* f = NULL;
    static int opened = 0;
    if (! opened)
    {
        opened = 1;
#if defined(_WIN32)
        char path[1024] = { 0 };
        if (GetEnvironmentVariableA ("LYNX_TEST_EXT_LOG", path, sizeof (path)) > 0)
            f = fopen (path, "a");
#else
        const char* path = getenv ("LYNX_TEST_EXT_LOG");
        if (path != NULL)
            f = fopen (path, "a");
#endif
    }
    return f;
}
static void say (const char* text)
{
    FILE* f = log_file();
    if (f != NULL)
    {
        fputs (text, f);
        fflush (f);
    }
}

typedef struct
{
    clap_plugin_t plugin;
    const clap_host_t* host;
    double level, width;
    int calls, logged;
    atomic_int tasks;
    atomic_ullong threadIds[8];
} Ext;

static Ext* self_of (const clap_plugin_t* p) { return (Ext*) p->plugin_data; }

static unsigned long long thread_id (void)
{
#if defined(_WIN32)
    return (unsigned long long) GetCurrentThreadId();
#else
    return (unsigned long long) (uintptr_t) pthread_self();
#endif
}

/* audio ports */
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

/* params */
static uint32_t params_count (const clap_plugin_t* p) { (void) p; return 2; }
static bool params_info (const clap_plugin_t* p, uint32_t index, clap_param_info_t* info)
{
    (void) p;
    if (index > 1) return false;
    memset (info, 0, sizeof (*info));
    info->id = index == 0 ? LEVEL_ID : WIDTH_ID;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    strcpy (info->name, index == 0 ? "Level" : "Width");
    info->min_value = 0.0;
    info->max_value = 1.0;
    info->default_value = 0.5;
    return true;
}
static bool params_value (const clap_plugin_t* p, clap_id id, double* value)
{
    Ext* e = self_of (p);
    if (id == LEVEL_ID) { *value = e->level; return true; }
    if (id == WIDTH_ID) { *value = e->width; return true; }
    return false;
}
static bool params_to_text (const clap_plugin_t* p, clap_id id, double v, char* out, uint32_t size)
{
    (void) p; (void) id;
    snprintf (out, size, "%.3f", v);
    return true;
}
static bool params_from_text (const clap_plugin_t* p, clap_id id, const char* text, double* v)
{
    (void) p; (void) id;
    *v = atof (text);
    return true;
}
static void apply_event (Ext* e, const clap_event_header_t* h, uint32_t frames)
{
    if (h->space_id != CLAP_CORE_EVENT_SPACE_ID || h->type != CLAP_EVENT_PARAM_VALUE) return;
    const clap_event_param_value_t* v = (const clap_event_param_value_t*) h;
    if (v->param_id == LEVEL_ID)
    {
        e->level = v->value;
        if (e->logged < 400)
        {
            char line[96];
            snprintf (line, sizeof (line), "P %u %.6f n=%u\n", (unsigned) h->time, v->value, (unsigned) frames);
            say (line);
            ++e->logged;
        }
    }
    else if (v->param_id == WIDTH_ID)
        e->width = v->value;
}
static void params_flush (const clap_plugin_t* p, const clap_input_events_t* in, const clap_output_events_t* out)
{
    (void) out;
    for (uint32_t i = 0; i < in->size (in); ++i)
        apply_event (self_of (p), in->get (in, i), 0);
}
static const clap_plugin_params_t params = { params_count, params_info, params_value, params_to_text, params_from_text, params_flush };

/* state + state-context */
static bool write_state (Ext* e, const clap_ostream_t* s, uint32_t context)
{
    unsigned char data[12];
    float level = (float) e->level, width = (float) e->width;
    memcpy (data, "LXE", 3);
    data[3] = (unsigned char) context;
    memcpy (data + 4, &level, 4);
    memcpy (data + 8, &width, 4);
    return s->write (s, data, sizeof (data)) == (int64_t) sizeof (data);
}
static bool read_state (Ext* e, const clap_istream_t* s, uint32_t* context)
{
    unsigned char data[12];
    if (s->read (s, data, sizeof (data)) != (int64_t) sizeof (data) || memcmp (data, "LXE", 3) != 0) return false;
    float level, width;
    memcpy (&level, data + 4, 4);
    memcpy (&width, data + 8, 4);
    e->level = level;
    e->width = width;
    *context = data[3];
    return true;
}
static bool state_save (const clap_plugin_t* p, const clap_ostream_t* s) { say ("save context=0\n"); return write_state (self_of (p), s, 0); }
static bool state_load (const clap_plugin_t* p, const clap_istream_t* s)
{
    uint32_t saved = 0;
    say ("load context=0\n");
    return read_state (self_of (p), s, &saved);
}
static const clap_plugin_state_t state = { state_save, state_load };
static bool context_save (const clap_plugin_t* p, const clap_ostream_t* s, uint32_t context)
{
    char line[48];
    snprintf (line, sizeof (line), "save context=%u\n", (unsigned) context);
    say (line);
    return write_state (self_of (p), s, context);
}
static bool context_load (const clap_plugin_t* p, const clap_istream_t* s, uint32_t context)
{
    uint32_t saved = 0;
    char line[64];
    const bool ok = read_state (self_of (p), s, &saved);
    snprintf (line, sizeof (line), "load context=%u saved_with=%u\n", (unsigned) context, (unsigned) saved);
    say (line);
    return ok;
}
static const clap_plugin_state_context_t state_context = { context_save, context_load };

/* latency 0, tail, render, voice info */
static uint32_t latency_get (const clap_plugin_t* p) { (void) p; return 0; }
static const clap_plugin_latency_t latency = { latency_get };
static uint32_t tail_get (const clap_plugin_t* p) { (void) p; return 4800; }
static const clap_plugin_tail_t tail = { tail_get };
static bool render_hard (const clap_plugin_t* p) { (void) p; return false; }
static bool render_set (const clap_plugin_t* p, clap_plugin_render_mode mode)
{
    (void) p;
    say (mode == CLAP_RENDER_OFFLINE ? "render offline\n" : "render realtime\n");
    return true;
}
static const clap_plugin_render_t render = { render_hard, render_set };
static bool voice_get (const clap_plugin_t* p, clap_voice_info_t* info)
{
    (void) p;
    info->voice_count = 3;
    info->voice_capacity = 16;
    info->flags = CLAP_VOICE_INFO_SUPPORTS_OVERLAPPING_NOTES;
    return true;
}
static const clap_plugin_voice_info_t voice_info = { voice_get };

/* thread pool */
static void pool_exec (const clap_plugin_t* p, uint32_t task)
{
    Ext* e = self_of (p);
    atomic_fetch_add (&e->tasks, 1);
    if (task < 8) atomic_store (&e->threadIds[task], thread_id());
    volatile double x = 0.0; /* a little work so the tasks overlap */
    for (int i = 0; i < 20000; ++i) x += (double) i * 1e-9;
}
static const clap_plugin_thread_pool_t thread_pool = { pool_exec };

/* remote controls */
static uint32_t remote_count (const clap_plugin_t* p) { (void) p; return 1; }
static bool remote_get (const clap_plugin_t* p, uint32_t index, clap_remote_controls_page_t* page)
{
    (void) p;
    if (index != 0) return false;
    memset (page, 0, sizeof (*page));
    strcpy (page->section_name, "Test");
    strcpy (page->page_name, "Main");
    page->page_id = 1;
    for (int k = 0; k < CLAP_REMOTE_CONTROLS_COUNT; ++k) page->param_ids[k] = CLAP_INVALID_ID;
    page->param_ids[0] = WIDTH_ID; /* (the page's own order: Width first, then Level) */
    page->param_ids[1] = LEVEL_ID;
    return true;
}
static const clap_plugin_remote_controls_t remote_controls = { remote_count, remote_get };

/* preset load */
static bool preset_load (const clap_plugin_t* p, uint32_t kind, const char* location, const char* key)
{
    Ext* e = self_of (p);
    (void) location;
    if (kind != CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN || key == NULL) return false;
    double v = strcmp (key, "low") == 0 ? 0.25 : strcmp (key, "mid") == 0 ? 0.5 : strcmp (key, "high") == 0 ? 0.75 : -1.0;
    if (v < 0.0) return false;
    e->level = v;
    char line[64];
    snprintf (line, sizeof (line), "preset %s level=%.2f\n", key, v);
    say (line);
    const clap_host_preset_load_t* host = (const clap_host_preset_load_t*) e->host->get_extension (e->host, CLAP_EXT_PRESET_LOAD);
    if (host != NULL) host->loaded (e->host, kind, location, key);
    return true;
}
static const clap_plugin_preset_load_t preset_load_ext = { preset_load };

/* plugin */
static bool p_init (const clap_plugin_t* p) { (void) p; return true; }
static void p_destroy (const clap_plugin_t* p) { free (self_of (p)); }
static bool p_activate (const clap_plugin_t* p, double r, uint32_t a, uint32_t b) { (void) p; (void) r; (void) a; (void) b; return true; }
static void p_deactivate (const clap_plugin_t* p) { (void) p; }
static bool p_start (const clap_plugin_t* p) { (void) p; return true; }
static void p_stop (const clap_plugin_t* p) { (void) p; }
static void p_reset (const clap_plugin_t* p) { (void) p; }
static clap_process_status p_process (const clap_plugin_t* p, const clap_process_t* proc)
{
    Ext* e = self_of (p);
    const uint32_t n = proc->frames_count;
    const uint32_t events = proc->in_events->size (proc->in_events);
    uint32_t next = 0;
    float* l = proc->audio_outputs_count > 0 && proc->audio_outputs[0].channel_count > 0 ? proc->audio_outputs[0].data32[0] : NULL;
    float* r = proc->audio_outputs_count > 0 && proc->audio_outputs[0].channel_count > 1 ? proc->audio_outputs[0].data32[1] : NULL;
    for (uint32_t i = 0; i < n; ++i)
    {
        while (next < events)
        {
            const clap_event_header_t* h = proc->in_events->get (proc->in_events, next);
            if (h->time > i) break;
            apply_event (e, h, n);
            ++next;
        }
        if (l != NULL) l[i] = (float) e->level;
        if (r != NULL) r[i] = (float) e->level;
    }
    for (; next < events; ++next)
        apply_event (e, proc->in_events->get (proc->in_events, next), n);

    if (++e->calls == 10)
    {
        const clap_host_thread_pool_t* pool = (const clap_host_thread_pool_t*) e->host->get_extension (e->host, CLAP_EXT_THREAD_POOL);
        atomic_store (&e->tasks, 0);
        for (int k = 0; k < 8; ++k) atomic_store (&e->threadIds[k], 0ULL);
        const bool ok = pool != NULL && pool->request_exec (e->host, 8);
        int threads = 0;
        for (int a = 0; a < 8; ++a)
        {
            const unsigned long long id = atomic_load (&e->threadIds[a]);
            int seen = 0;
            for (int b = 0; b < a; ++b) seen = seen || atomic_load (&e->threadIds[b]) == id;
            if (id != 0 && ! seen) ++threads;
        }
        char line[96];
        snprintf (line, sizeof (line), "pool ok=%d tasks=%d threads=%d\n", ok ? 1 : 0, atomic_load (&e->tasks), threads);
        say (line);
    }
    return CLAP_PROCESS_CONTINUE;
}
static const void* p_extension (const clap_plugin_t* p, const char* id)
{
    (void) p;
    if (strcmp (id, CLAP_EXT_AUDIO_PORTS) == 0) return &audio_ports;
    if (strcmp (id, CLAP_EXT_PARAMS) == 0) return &params;
    if (strcmp (id, CLAP_EXT_STATE) == 0) return &state;
    if (strcmp (id, CLAP_EXT_STATE_CONTEXT) == 0) return &state_context;
    if (strcmp (id, CLAP_EXT_LATENCY) == 0) return &latency;
    if (strcmp (id, CLAP_EXT_TAIL) == 0) return &tail;
    if (strcmp (id, CLAP_EXT_RENDER) == 0) return &render;
    if (strcmp (id, CLAP_EXT_VOICE_INFO) == 0) return &voice_info;
    if (strcmp (id, CLAP_EXT_THREAD_POOL) == 0) return &thread_pool;
    if (strcmp (id, CLAP_EXT_REMOTE_CONTROLS) == 0) return &remote_controls;
    if (strcmp (id, CLAP_EXT_PRESET_LOAD) == 0) return &preset_load_ext;
    return NULL;
}
static void p_main_thread (const clap_plugin_t* p) { (void) p; }

static uint32_t f_count (const clap_plugin_factory_t* f) { (void) f; return 1; }
static const clap_plugin_descriptor_t* f_descriptor (const clap_plugin_factory_t* f, uint32_t i) { (void) f; return i == 0 ? &descriptor : NULL; }
static const clap_plugin_t* f_create (const clap_plugin_factory_t* f, const clap_host_t* host, const char* id)
{
    (void) f;
    if (strcmp (id, descriptor.id) != 0) return NULL;
    Ext* e = (Ext*) calloc (1, sizeof (Ext));
    e->host = host;
    e->level = 0.5;
    e->width = 0.5;
    clap_plugin_t* p = &e->plugin;
    p->desc = &descriptor;
    p->plugin_data = e;
    p->init = p_init; p->destroy = p_destroy; p->activate = p_activate; p->deactivate = p_deactivate;
    p->start_processing = p_start; p->stop_processing = p_stop; p->reset = p_reset; p->process = p_process;
    p->get_extension = p_extension; p->on_main_thread = p_main_thread;
    return p;
}
static const clap_plugin_factory_t factory = { f_count, f_descriptor, f_create };

/* preset discovery: one provider with three presets inside the plugin */
static const clap_preset_discovery_provider_descriptor_t provider_descriptor = { CLAP_VERSION_INIT, "org.lynx.test-ext.presets", "LYNX Test Ext presets", "LYNX" };
static bool prov_init (const clap_preset_discovery_provider_t* p)
{
    const clap_preset_discovery_indexer_t* indexer = (const clap_preset_discovery_indexer_t*) p->provider_data;
    const clap_preset_discovery_location_t location = { CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT, "Factory", CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, NULL };
    return indexer->declare_location (indexer, &location);
}
static void prov_destroy (const clap_preset_discovery_provider_t* p) { free ((void*) p); }
static bool prov_metadata (const clap_preset_discovery_provider_t* p, uint32_t kind, const char* location, const clap_preset_discovery_metadata_receiver_t* r)
{
    (void) p; (void) location;
    if (kind != CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN) return false;
    static const char* const names[3] = { "Low", "Mid", "High" };
    static const char* const keys[3] = { "low", "mid", "high" };
    const clap_universal_plugin_id_t id = { "clap", "org.lynx.test-ext" };
    for (int i = 0; i < 3; ++i)
        if (r->begin_preset (r, names[i], keys[i]))
            r->add_plugin_id (r, &id);
    return true;
}
static const void* prov_extension (const clap_preset_discovery_provider_t* p, const char* id) { (void) p; (void) id; return NULL; }
static uint32_t d_count (const clap_preset_discovery_factory_t* f) { (void) f; return 1; }
static const clap_preset_discovery_provider_descriptor_t* d_descriptor (const clap_preset_discovery_factory_t* f, uint32_t i)
{
    (void) f;
    return i == 0 ? &provider_descriptor : NULL;
}
static const clap_preset_discovery_provider_t* d_create (const clap_preset_discovery_factory_t* f, const clap_preset_discovery_indexer_t* indexer, const char* id)
{
    (void) f;
    if (strcmp (id, provider_descriptor.id) != 0) return NULL;
    clap_preset_discovery_provider_t* p = (clap_preset_discovery_provider_t*) calloc (1, sizeof (*p));
    p->desc = &provider_descriptor;
    p->provider_data = (void*) indexer;
    p->init = prov_init;
    p->destroy = prov_destroy;
    p->get_metadata = prov_metadata;
    p->get_extension = prov_extension;
    return p;
}
static const clap_preset_discovery_factory_t discovery = { d_count, d_descriptor, d_create };

static bool e_init (const char* path) { (void) path; return true; }
static void e_deinit (void) {}
static const void* e_factory (const char* id)
{
    if (strcmp (id, CLAP_PLUGIN_FACTORY_ID) == 0) return &factory;
    if (strcmp (id, CLAP_PRESET_DISCOVERY_FACTORY_ID) == 0) return &discovery;
    return NULL;
}

CLAP_EXPORT const clap_plugin_entry_t clap_entry = { CLAP_VERSION_INIT, e_init, e_deinit, e_factory };
