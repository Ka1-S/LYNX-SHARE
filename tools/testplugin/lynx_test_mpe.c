/* A minimal CLAP instrument that writes down every note, note expression and MIDI event the host sends it (issue #108,
 * MPE stage 1): LYNX's wrapper "MPE" switch must reach a CLAP plugin that takes CLAP notes as one channel per note
 * with tuning / brightness / pressure note expressions. Silent; the log goes to the file named by the environment
 * variable LYNX_TEST_MPE_REPORT when the plugin is destroyed.
 *
 *   zig cc -shared -O2 -target x86_64-windows-gnu -Ithird_party/clap/include tools/testplugin/lynx_test_mpe.c \
 *       -o "LYNX Test MPE.clap"
 *   LYNX.exe --mpetest "LYNX Test MPE.clap" report.txt
 *
 * Lines: "on <channel> <key> <velocity*1000>", "off <channel> <key>", "expr <expression id> <channel> <key>
 * <value*1000>", "midi <status> <data1> <data2>" (channels 0-based, as CLAP's). */
#include <clap/clap.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* const features[] = { CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER, NULL };

static const clap_plugin_descriptor_t descriptor = {
    CLAP_VERSION_INIT, "org.lynx.test-mpe", "LYNX Test MPE", "LYNX", "https://github.com/Ka1-S/LYNX-SHARE", "", "",
    "1.0.0", "Logs the notes, note expressions and MIDI it receives (MPE host test)", features
};

enum { logSize = 1 << 16 };

typedef struct
{
    clap_plugin_t plugin;
    char* log;
    int used;
} Logger;

static Logger* self (const clap_plugin_t* p) { return (Logger*) p->plugin_data; }

static void put (Logger* l, const char* line)
{
    const int n = (int) strlen (line);
    if (l->log != NULL && l->used + n < logSize)
    {
        memcpy (l->log + l->used, line, (size_t) n);
        l->used += n;
    }
}

static uint32_t audio_count (const clap_plugin_t* p, bool in) { (void) p; return in ? 0 : 1; }
static bool audio_get (const clap_plugin_t* p, uint32_t index, bool in, clap_audio_port_info_t* info)
{
    (void) p;
    if (in || index != 0) return false;
    memset (info, 0, sizeof (*info));
    info->id = 1;
    strcpy (info->name, "Out");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = 2;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}
static const clap_plugin_audio_ports_t audio_ports = { audio_count, audio_get };

static uint32_t note_count (const clap_plugin_t* p, bool in) { (void) p; return in ? 1 : 0; }
static bool note_get (const clap_plugin_t* p, uint32_t index, bool in, clap_note_port_info_t* info)
{
    (void) p;
    if (! in || index != 0) return false;
    memset (info, 0, sizeof (*info));
    info->id = 2;
    info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
    info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
    strcpy (info->name, "Notes");
    return true;
}
static const clap_plugin_note_ports_t note_ports = { note_count, note_get };

static bool p_init (const clap_plugin_t* p)
{
    Logger* l = self (p);
    l->log = (char*) malloc (logSize); /* (before any processing: the audio thread only copies into it) */
    l->used = 0;
    return l->log != NULL;
}
static void p_destroy (const clap_plugin_t* p)
{
    Logger* l = self (p);
    const char* path = getenv ("LYNX_TEST_MPE_REPORT");
    if (path != NULL && l->log != NULL)
    {
        FILE* f = fopen (path, "wb");
        if (f != NULL)
        {
            fwrite (l->log, 1, (size_t) l->used, f);
            fclose (f);
        }
    }
    free (l->log);
    free (l);
}
static bool p_activate (const clap_plugin_t* p, double rate, uint32_t minFrames, uint32_t maxFrames)
{
    (void) p; (void) rate; (void) minFrames; (void) maxFrames;
    return true;
}
static void p_deactivate (const clap_plugin_t* p) { (void) p; }
static bool p_start (const clap_plugin_t* p) { (void) p; return true; }
static void p_stop (const clap_plugin_t* p) { (void) p; }
static void p_reset (const clap_plugin_t* p) { (void) p; }
static clap_process_status p_process (const clap_plugin_t* p, const clap_process_t* proc)
{
    Logger* l = self (p);
    char line[96];
    const uint32_t n = proc->in_events != NULL ? proc->in_events->size (proc->in_events) : 0;
    for (uint32_t i = 0; i < n; ++i)
    {
        const clap_event_header_t* h = proc->in_events->get (proc->in_events, i);
        if (h == NULL || h->space_id != CLAP_CORE_EVENT_SPACE_ID)
            continue;
        if (h->type == CLAP_EVENT_NOTE_ON || h->type == CLAP_EVENT_NOTE_OFF)
        {
            const clap_event_note_t* e = (const clap_event_note_t*) h;
            if (h->type == CLAP_EVENT_NOTE_ON)
                snprintf (line, sizeof line, "on %d %d %d\n", (int) e->channel, (int) e->key, (int) (e->velocity * 1000.0 + 0.5));
            else
                snprintf (line, sizeof line, "off %d %d\n", (int) e->channel, (int) e->key);
            put (l, line);
        }
        else if (h->type == CLAP_EVENT_NOTE_EXPRESSION)
        {
            const clap_event_note_expression_t* e = (const clap_event_note_expression_t*) h;
            const double v = e->value * 1000.0;
            snprintf (line, sizeof line, "expr %d %d %d %d\n", (int) e->expression_id, (int) e->channel, (int) e->key,
                      (int) (v >= 0.0 ? v + 0.5 : v - 0.5));
            put (l, line);
        }
        else if (h->type == CLAP_EVENT_MIDI)
        {
            const clap_event_midi_t* e = (const clap_event_midi_t*) h;
            snprintf (line, sizeof line, "midi %d %d %d\n", (int) e->data[0], (int) e->data[1], (int) e->data[2]);
            put (l, line);
        }
    }
    if (proc->audio_outputs_count >= 1)
        for (uint32_t c = 0; c < proc->audio_outputs[0].channel_count; ++c)
            if (proc->audio_outputs[0].data32 != NULL && proc->audio_outputs[0].data32[c] != NULL)
                memset (proc->audio_outputs[0].data32[c], 0, sizeof (float) * proc->frames_count);
    return CLAP_PROCESS_CONTINUE;
}
static const void* p_extension (const clap_plugin_t* p, const char* id)
{
    (void) p;
    if (strcmp (id, CLAP_EXT_AUDIO_PORTS) == 0) return &audio_ports;
    if (strcmp (id, CLAP_EXT_NOTE_PORTS) == 0) return &note_ports;
    return NULL;
}
static void p_main_thread (const clap_plugin_t* p) { (void) p; }

static uint32_t f_count (const clap_plugin_factory_t* f) { (void) f; return 1; }
static const clap_plugin_descriptor_t* f_descriptor (const clap_plugin_factory_t* f, uint32_t i) { (void) f; return i == 0 ? &descriptor : NULL; }
static const clap_plugin_t* f_create (const clap_plugin_factory_t* f, const clap_host_t* host, const char* id)
{
    (void) f; (void) host;
    if (strcmp (id, descriptor.id) != 0) return NULL;
    Logger* l = (Logger*) calloc (1, sizeof (Logger));
    clap_plugin_t* p = &l->plugin;
    p->desc = &descriptor;
    p->plugin_data = l;
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
