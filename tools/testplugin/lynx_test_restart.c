/* A minimal CLAP effect that asks the host for a restart (clap_host_t::request_restart) every 8 blocks, the way a
 * plugin does when its latency changes (lookahead switched on, a new preset), to test LYNX's restart handshake
 * (PR #29): the host must never deactivate the plugin while its process() call is running.
 *   zig cc -shared -O2 -target x86_64-windows-gnu -Ithird_party/clap/include tools/testplugin/lynx_test_restart.c \
 *       -o "LYNX Test Restart.clap"
 *   LYNX.exe --claprestarttest "LYNX Test Restart.clap" report.txt
 * Each activation reports another latency (64 to 448 samples, cycling), so the host's delay
 * compensation must follow every restart. After 48 requests the plugin stops asking (the latency then stays).
 * With LYNX_TEST_RESTART_REPORT set, each instance appends a line with its counters to that file when destroyed.
 * Threads (issue #46): like u-he's plugins (Presswerk), it asks the host's thread_check whether start_processing /
 * stop_processing run on the audio thread and aborts the process when they don't ("Host called the method
 * clap_plugin.stop_processing() on wrong thread!"). Activate / deactivate off the main thread, deactivate while
 * processing and start / stop out of order are counted (wrong_thread, misordered).
 */
#include <clap/clap.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* const features[] = { CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY, NULL };

static const clap_plugin_descriptor_t descriptor = {
    CLAP_VERSION_INIT, "org.lynx.test-restart", "LYNX Test Restart", "LYNX", "https://github.com/Ka1-S/LYNX-SHARE", "", "",
    "1.0.0", "Requests a restart every 8 blocks (host restart handshake test)", features
};

enum { maxRequests = 48 };

typedef struct
{
    clap_plugin_t plugin;
    const clap_host_t* host;
    atomic_int inProcess;
    atomic_int active;
    atomic_int activations, deactivations, overlapped, inactiveBlocks, requests, blocks;
    atomic_int processing, starts, stops, wrongThread, misordered;
    const clap_host_thread_check_t* threads;
    float* dsp; /* allocated in activate, freed in deactivate (what makes a mid-process deactivate crash) */
} Restart;

static Restart* self (const clap_plugin_t* p) { return (Restart*) p->plugin_data; }

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

static uint32_t latency_get (const clap_plugin_t* p) { return (uint32_t) (64 * (1 + atomic_load (&self (p)->activations) % 7)); }
static const clap_plugin_latency_t latency = { latency_get };

/* main = 1: must be the main thread; main = 0: must be an audio thread (what the host's thread_check says) */
static bool on_thread (Restart* r, int main)
{
    if (r->threads == NULL)
        return true;
    return main ? r->threads->is_main_thread (r->host) : r->threads->is_audio_thread (r->host);
}
static void must_be_audio_thread (Restart* r, const char* method)
{
    if (on_thread (r, 0))
        return;
    fprintf (stderr, "[CLAP] Host called the method clap_plugin.%s() on wrong thread! It must be called on audio thread!\n", method);
    fflush (stderr);
    abort(); /* what u-he's plugins do (issue #46) */
}

static bool p_init (const clap_plugin_t* p)
{
    Restart* r = self (p);
    r->threads = (const clap_host_thread_check_t*) r->host->get_extension (r->host, CLAP_EXT_THREAD_CHECK);
    return true;
}
static void p_destroy (const clap_plugin_t* p)
{
    Restart* r = self (p);
    if (atomic_load (&r->processing) != 0 || atomic_load (&r->active) != 0)
        atomic_fetch_add (&r->misordered, 1); /* destroyed while active or processing */
    const char* path = getenv ("LYNX_TEST_RESTART_REPORT");
    if (path != NULL && *path != 0)
    {
        FILE* f = fopen (path, "a"); /* one line per instance */
        if (f != NULL)
        {
            fprintf (f, "requests=%d activations=%d deactivations=%d overlapped=%d inactive_blocks=%d blocks=%d latency=%u "
                        "starts=%d stops=%d wrong_thread=%d misordered=%d\n",
                     atomic_load (&r->requests), atomic_load (&r->activations), atomic_load (&r->deactivations),
                     atomic_load (&r->overlapped), atomic_load (&r->inactiveBlocks), atomic_load (&r->blocks), latency_get (p),
                     atomic_load (&r->starts), atomic_load (&r->stops), atomic_load (&r->wrongThread), atomic_load (&r->misordered));
            fclose (f);
        }
    }
    free (r->dsp);
    free (r);
}
static bool p_activate (const clap_plugin_t* p, double rate, uint32_t minFrames, uint32_t maxFrames)
{
    Restart* r = self (p);
    (void) rate; (void) minFrames;
    if (! on_thread (r, 1))
        atomic_fetch_add (&r->wrongThread, 1);
    if (atomic_load (&r->active) != 0)
        atomic_fetch_add (&r->misordered, 1);
    free (r->dsp);
    r->dsp = (float*) calloc (maxFrames > 0 ? maxFrames : 1, sizeof (float));
    atomic_fetch_add (&r->activations, 1);
    atomic_store (&r->active, 1);
    return r->dsp != NULL;
}
static void p_deactivate (const clap_plugin_t* p)
{
    Restart* r = self (p);
    if (atomic_load (&r->inProcess) != 0)
        atomic_fetch_add (&r->overlapped, 1);
    if (! on_thread (r, 1))
        atomic_fetch_add (&r->wrongThread, 1);
    if (atomic_load (&r->processing) != 0 || atomic_load (&r->active) == 0)
        atomic_fetch_add (&r->misordered, 1); /* deactivated while processing (or twice) */
    atomic_store (&r->active, 0);
    atomic_fetch_add (&r->deactivations, 1);
    /* (not freed here: a host that breaks the rule is counted, not crashed; the next activate replaces it) */
}
static bool p_start (const clap_plugin_t* p)
{
    Restart* r = self (p);
    must_be_audio_thread (r, "start_processing");
    if (atomic_load (&r->active) == 0 || atomic_exchange (&r->processing, 1) != 0)
        atomic_fetch_add (&r->misordered, 1);
    atomic_fetch_add (&r->starts, 1);
    return true;
}
static void p_stop (const clap_plugin_t* p)
{
    Restart* r = self (p);
    must_be_audio_thread (r, "stop_processing");
    if (atomic_exchange (&r->processing, 0) == 0)
        atomic_fetch_add (&r->misordered, 1);
    atomic_fetch_add (&r->stops, 1);
}
static void p_reset (const clap_plugin_t* p) { (void) p; }
static clap_process_status p_process (const clap_plugin_t* p, const clap_process_t* proc)
{
    Restart* r = self (p);
    atomic_store (&r->inProcess, 1);
    if (atomic_load (&r->active) == 0 || atomic_load (&r->processing) == 0)
        atomic_fetch_add (&r->inactiveBlocks, 1); /* (process outside start / stop_processing counts too) */
    const int block = atomic_fetch_add (&r->blocks, 1) + 1;
    if (block % 8 == 0 && atomic_load (&r->requests) < maxRequests)
    {
        atomic_fetch_add (&r->requests, 1);
        r->host->request_restart (r->host);
    }

    /* some work, long enough (~0.2 ms) that a restart from the main thread can land inside it */
    volatile unsigned spin = 0;
    for (unsigned i = 0; i < 200000u; ++i)
        spin += i;

    if (proc->audio_inputs_count >= 1 && proc->audio_outputs_count >= 1)
        for (uint32_t c = 0; c < 2 && c < proc->audio_inputs[0].channel_count && c < proc->audio_outputs[0].channel_count; ++c)
            for (uint32_t i = 0; i < proc->frames_count; ++i)
                proc->audio_outputs[0].data32[c][i] = proc->audio_inputs[0].data32[c][i];

    if (atomic_load (&r->active) == 0)
        atomic_fetch_add (&r->inactiveBlocks, 1); /* (deactivated while this call ran) */
    atomic_store (&r->inProcess, 0);
    return CLAP_PROCESS_CONTINUE;
}
static const void* p_extension (const clap_plugin_t* p, const char* id)
{
    (void) p;
    if (strcmp (id, CLAP_EXT_AUDIO_PORTS) == 0) return &audio_ports;
    if (strcmp (id, CLAP_EXT_LATENCY) == 0) return &latency;
    return NULL;
}
static void p_main_thread (const clap_plugin_t* p) { (void) p; }

static uint32_t f_count (const clap_plugin_factory_t* f) { (void) f; return 1; }
static const clap_plugin_descriptor_t* f_descriptor (const clap_plugin_factory_t* f, uint32_t i) { (void) f; return i == 0 ? &descriptor : NULL; }
static const clap_plugin_t* f_create (const clap_plugin_factory_t* f, const clap_host_t* host, const char* id)
{
    (void) f;
    if (strcmp (id, descriptor.id) != 0) return NULL;
    Restart* r = (Restart*) calloc (1, sizeof (Restart));
    r->host = host;
    clap_plugin_t* p = &r->plugin;
    p->desc = &descriptor;
    p->plugin_data = r;
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
