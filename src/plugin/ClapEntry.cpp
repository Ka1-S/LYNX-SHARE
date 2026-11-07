// LYNX's plugins as CLAP (round 180b): one module holds every entry of plugin/ExportCatalog.h. JUCE 8 has no CLAP client,
// so this is the small bridge from the CLAP ABI (third_party/clap, MIT) to a juce::AudioProcessor. 32-bit float audio.

#include <atomic>
#include <cstring>
#include <memory>
#include <vector>

#include <clap/clap.h>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "plugin/ExportProcessor.h"

namespace lynx::plugexport
{
namespace
{
// The host owns the message loop. Windows: JUCE's hidden window is pumped by it. Linux: a host timer dispatches JUCE's queue.
struct JuceRuntime
{
    juce::ScopedJuceInitialiser_GUI gui;
};

class ClapPlugin final : private juce::AudioProcessorListener
{
public:
    ClapPlugin (const clap_host_t* h, const Entry& e, const clap_plugin_descriptor_t* descriptor) : host (h), entry (e)
    {
        plugin.desc = descriptor;
        plugin.plugin_data = this;
        plugin.init = [] (const clap_plugin_t* p) { return self (p)->init(); };
        plugin.destroy = [] (const clap_plugin_t* p) { delete self (p); };
        plugin.activate = [] (const clap_plugin_t* p, double rate, std::uint32_t, std::uint32_t maxFrames) {
            auto* s = self (p);
            s->processor->setPlayConfigDetails (s->entry.instrument ? 0 : 2, 2, rate, (int) maxFrames);
            s->processor->prepareToPlay (rate, (int) maxFrames);
            s->midi.ensureSize (4096);
            return true;
        };
        plugin.deactivate = [] (const clap_plugin_t* p) { self (p)->processor->releaseResources(); };
        plugin.start_processing = [] (const clap_plugin_t*) { return true; };
        plugin.stop_processing = [] (const clap_plugin_t*) {};
        plugin.reset = [] (const clap_plugin_t* p) { self (p)->processor->reset(); };
        plugin.process = [] (const clap_plugin_t* p, const clap_process_t* process) { return self (p)->process (*process); };
        plugin.get_extension = [] (const clap_plugin_t* p, const char* id) { return self (p)->extension (id); };
        plugin.on_main_thread = [] (const clap_plugin_t*) {};
    }

    ~ClapPlugin() override
    {
        destroyGui();
        if (processor != nullptr)
            processor->removeListener (this);
        processor.reset();
        if (timerId != CLAP_INVALID_ID && hostTimer != nullptr)
            hostTimer->unregister_timer (host, timerId);
        runtime.reset();
    }

    clap_plugin_t plugin {};

private:
    static ClapPlugin* self (const clap_plugin_t* p) { return static_cast<ClapPlugin*> (p->plugin_data); }

    bool init()
    {
        runtime = std::make_unique<JuceRuntime>();
        processor.reset (makeProcessor (entry));
        processor->addListener (this);
        parameters = processor->getParameters();
        pending = std::make_unique<std::atomic<bool>[]> ((std::size_t) parameters.size());
        hostParams = static_cast<const clap_host_params_t*> (host->get_extension (host, CLAP_EXT_PARAMS));
        hostLatency = static_cast<const clap_host_latency_t*> (host->get_extension (host, CLAP_EXT_LATENCY));
        hostState = static_cast<const clap_host_state_t*> (host->get_extension (host, CLAP_EXT_STATE));
        hostTimer = static_cast<const clap_host_timer_support_t*> (host->get_extension (host, CLAP_EXT_TIMER_SUPPORT));
       #if JUCE_LINUX || JUCE_BSD
        if (hostTimer != nullptr && hostTimer->register_timer != nullptr)
            hostTimer->register_timer (host, 16, &timerId);
       #endif
        return true;
    }

    // ---- processor -> host
    void audioProcessorParameterChanged (juce::AudioProcessor*, int index, float) override
    {
        if (index >= 0 && index < parameters.size())
            pending[(std::size_t) index].store (true, std::memory_order_release);
        if (hostParams != nullptr && hostParams->request_flush != nullptr)
            hostParams->request_flush (host); // (any thread)
        if (hostState != nullptr && juce::MessageManager::existsAndIsCurrentThread())
            hostState->mark_dirty (host);
    }

    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails& details) override
    {
        if (details.latencyChanged && hostLatency != nullptr && juce::MessageManager::existsAndIsCurrentThread())
            hostLatency->changed (host);
    }

    void sendPending (const clap_output_events_t* out)
    {
        if (out == nullptr)
            return;
        for (int i = 0; i < parameters.size(); ++i)
            if (pending[(std::size_t) i].exchange (false, std::memory_order_acq_rel))
            {
                clap_event_param_value_t e {};
                e.header.size = sizeof (e);
                e.header.type = CLAP_EVENT_PARAM_VALUE;
                e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
                e.param_id = (clap_id) i;
                e.note_id = -1;
                e.port_index = -1;
                e.channel = -1;
                e.key = -1;
                e.value = parameters[i]->getValue();
                out->try_push (out, &e.header);
            }
    }

    void takeEvent (const clap_event_header_t& header)
    {
        if (header.space_id != CLAP_CORE_EVENT_SPACE_ID)
            return;
        const auto at = (int) header.time;
        switch (header.type)
        {
            case CLAP_EVENT_PARAM_VALUE:
            {
                const auto& e = reinterpret_cast<const clap_event_param_value_t&> (header);
                if ((int) e.param_id < parameters.size())
                    parameters[(int) e.param_id]->setValue ((float) juce::jlimit (0.0, 1.0, e.value));
                break;
            }
            case CLAP_EVENT_NOTE_ON:
            {
                const auto& e = reinterpret_cast<const clap_event_note_t&> (header);
                midi.addEvent (juce::MidiMessage::noteOn (juce::jlimit (1, 16, e.channel + 1), e.key, (float) e.velocity), at);
                break;
            }
            case CLAP_EVENT_NOTE_OFF:
            case CLAP_EVENT_NOTE_CHOKE:
            {
                const auto& e = reinterpret_cast<const clap_event_note_t&> (header);
                midi.addEvent (juce::MidiMessage::noteOff (juce::jlimit (1, 16, e.channel + 1), e.key, (float) e.velocity), at);
                break;
            }
            case CLAP_EVENT_MIDI:
            {
                const auto& e = reinterpret_cast<const clap_event_midi_t&> (header);
                midi.addEvent (e.data, 3, at);
                break;
            }
            default:
                break;
        }
    }

    clap_process_status process (const clap_process_t& p)
    {
        const auto frames = (int) p.frames_count;
        if (p.audio_outputs_count == 0 || p.audio_outputs[0].data32 == nullptr || p.audio_outputs[0].channel_count < 2)
            return CLAP_PROCESS_ERROR;

        midi.clear();
        if (p.in_events != nullptr)
            for (std::uint32_t i = 0, n = p.in_events->size (p.in_events); i < n; ++i)
                if (const auto* header = p.in_events->get (p.in_events, i))
                    takeEvent (*header);
        sendPending (p.out_events);

        auto* const* outs = p.audio_outputs[0].data32;
        if (! entry.instrument && p.audio_inputs_count > 0 && p.audio_inputs[0].data32 != nullptr)
            for (std::uint32_t c = 0; c < 2; ++c)
            {
                const auto* in = p.audio_inputs[0].data32[juce::jmin (c, p.audio_inputs[0].channel_count - 1)];
                if (in != outs[c])
                    std::memcpy (outs[c], in, sizeof (float) * (std::size_t) frames);
            }
        else
            for (std::uint32_t c = 0; c < 2; ++c)
                std::memset (outs[c], 0, sizeof (float) * (std::size_t) frames);

        position.transport = p.transport;
        processor->setPlayHead (&position);
        juce::AudioBuffer<float> buffer (outs, 2, frames);
        processor->processBlock (buffer, midi);
        return entry.instrument ? CLAP_PROCESS_CONTINUE : CLAP_PROCESS_CONTINUE_IF_NOT_QUIET;
    }

    struct PlayHead final : juce::AudioPlayHead
    {
        const clap_event_transport_t* transport = nullptr;

        juce::Optional<PositionInfo> getPosition() const override
        {
            if (transport == nullptr)
                return {};
            PositionInfo info;
            if ((transport->flags & CLAP_TRANSPORT_HAS_TEMPO) != 0)
                info.setBpm (transport->tempo);
            if ((transport->flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE) != 0)
                info.setPpqPosition ((double) transport->song_pos_beats / (double) CLAP_BEATTIME_FACTOR);
            info.setIsPlaying ((transport->flags & CLAP_TRANSPORT_IS_PLAYING) != 0);
            return info;
        }
    };

    // ---- extensions
    const void* extension (const char* id)
    {
        static const clap_plugin_audio_ports_t audioPorts {
            [] (const clap_plugin_t* p, bool isInput) -> std::uint32_t { return isInput && self (p)->entry.instrument ? 0u : 1u; },
            [] (const clap_plugin_t*, std::uint32_t index, bool isInput, clap_audio_port_info_t* info) {
                if (index != 0)
                    return false;
                std::memset (info, 0, sizeof (*info));
                info->id = isInput ? 0 : 1;
                std::strncpy (info->name, isInput ? "Input" : "Output", sizeof (info->name) - 1);
                info->flags = CLAP_AUDIO_PORT_IS_MAIN;
                info->channel_count = 2;
                info->port_type = CLAP_PORT_STEREO;
                info->in_place_pair = CLAP_INVALID_ID;
                return true;
            }
        };
        static const clap_plugin_note_ports_t notePorts {
            [] (const clap_plugin_t* p, bool isInput) -> std::uint32_t { return isInput && self (p)->entry.instrument ? 1u : 0u; },
            [] (const clap_plugin_t* p, std::uint32_t index, bool isInput, clap_note_port_info_t* info) {
                if (index != 0 || ! isInput || ! self (p)->entry.instrument)
                    return false;
                std::memset (info, 0, sizeof (*info));
                info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
                info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
                std::strncpy (info->name, "Notes", sizeof (info->name) - 1);
                return true;
            }
        };
        static const clap_plugin_params_t paramsExt {
            [] (const clap_plugin_t* p) -> std::uint32_t { return (std::uint32_t) self (p)->parameters.size(); },
            [] (const clap_plugin_t* p, std::uint32_t index, clap_param_info_t* info) {
                auto* s = self (p);
                if ((int) index >= s->parameters.size())
                    return false;
                const auto* param = s->parameters[(int) index];
                std::memset (info, 0, sizeof (*info));
                info->id = index;
                info->flags = CLAP_PARAM_IS_AUTOMATABLE;
                param->getName ((int) sizeof (info->name) - 1).copyToUTF8 (info->name, sizeof (info->name));
                info->min_value = 0.0;
                info->max_value = 1.0;
                info->default_value = param->getDefaultValue();
                return true;
            },
            [] (const clap_plugin_t* p, clap_id id, double* value) {
                auto* s = self (p);
                if ((int) id >= s->parameters.size())
                    return false;
                *value = s->parameters[(int) id]->getValue();
                return true;
            },
            [] (const clap_plugin_t* p, clap_id id, double value, char* out, std::uint32_t size) {
                auto* s = self (p);
                if ((int) id >= s->parameters.size())
                    return false;
                s->parameters[(int) id]->getText ((float) value, (int) size - 1).copyToUTF8 (out, size);
                return true;
            },
            [] (const clap_plugin_t* p, clap_id id, const char* text, double* value) {
                auto* s = self (p);
                if ((int) id >= s->parameters.size())
                    return false;
                *value = s->parameters[(int) id]->getValueForText (juce::String::fromUTF8 (text));
                return true;
            },
            [] (const clap_plugin_t* p, const clap_input_events_t* in, const clap_output_events_t* out) {
                auto* s = self (p);
                if (in != nullptr)
                    for (std::uint32_t i = 0, n = in->size (in); i < n; ++i)
                        if (const auto* header = in->get (in, i); header != nullptr && header->type == CLAP_EVENT_PARAM_VALUE)
                            s->takeEvent (*header);
                s->sendPending (out);
            }
        };
        static const clap_plugin_latency_t latency {
            [] (const clap_plugin_t* p) -> std::uint32_t { return (std::uint32_t) self (p)->processor->getLatencySamples(); }
        };
        static const clap_plugin_state_t state {
            [] (const clap_plugin_t* p, const clap_ostream_t* stream) {
                juce::MemoryBlock block;
                self (p)->processor->getStateInformation (block);
                auto* data = static_cast<const char*> (block.getData());
                for (std::size_t done = 0; done < block.getSize();)
                {
                    const auto n = stream->write (stream, data + done, block.getSize() - done);
                    if (n <= 0)
                        return false;
                    done += (std::size_t) n;
                }
                return true;
            },
            [] (const clap_plugin_t* p, const clap_istream_t* stream) {
                juce::MemoryOutputStream all;
                char chunk[4096];
                for (;;)
                {
                    const auto n = stream->read (stream, chunk, sizeof (chunk));
                    if (n < 0)
                        return false;
                    if (n == 0)
                        break;
                    all.write (chunk, (std::size_t) n);
                }
                auto* s = self (p);
                if (! loadState (*s->processor, all.getData(), (int) all.getDataSize()))
                    return false;
                if (s->hostParams != nullptr) // (CLAP: the values changed, the host must read them again)
                    s->hostParams->rescan (s->host, CLAP_PARAM_RESCAN_VALUES);
                return true;
            }
        };
        static const clap_plugin_gui_t gui {
            [] (const clap_plugin_t*, const char* api, bool floating) { return ! floating && std::strcmp (api, nativeApi()) == 0; },
            [] (const clap_plugin_t*, const char** api, bool* floating) { *api = nativeApi(); *floating = false; return true; },
            [] (const clap_plugin_t* p, const char* api, bool floating) {
                if (floating || std::strcmp (api, nativeApi()) != 0)
                    return false;
                self (p)->editor.reset (self (p)->processor->createEditorIfNeeded());
                return self (p)->editor != nullptr;
            },
            [] (const clap_plugin_t* p) { self (p)->destroyGui(); },
            [] (const clap_plugin_t*, double) { return false; },
            [] (const clap_plugin_t* p, std::uint32_t* w, std::uint32_t* h) {
                auto* s = self (p);
                if (s->editor == nullptr)
                    return false;
                *w = (std::uint32_t) s->editor->getWidth();
                *h = (std::uint32_t) s->editor->getHeight();
                return true;
            },
            [] (const clap_plugin_t*) { return false; },
            [] (const clap_plugin_t*, clap_gui_resize_hints_t*) { return false; },
            [] (const clap_plugin_t*, std::uint32_t*, std::uint32_t*) { return true; },
            [] (const clap_plugin_t* p, std::uint32_t w, std::uint32_t h) {
                auto* s = self (p);
                if (s->editor != nullptr)
                    s->editor->setSize ((int) w, (int) h);
                return s->editor != nullptr;
            },
            [] (const clap_plugin_t* p, const clap_window_t* window) {
                auto* s = self (p);
                if (s->editor == nullptr)
                    return false;
                s->editor->setOpaque (true);
               #if JUCE_WINDOWS
                s->editor->addToDesktop (0, window->win32);
               #else
                s->editor->addToDesktop (0, reinterpret_cast<void*> (window->x11));
               #endif
                s->editor->setVisible (true);
                return true;
            },
            [] (const clap_plugin_t*, const clap_window_t*) { return false; },
            [] (const clap_plugin_t*, const char*) {},
            [] (const clap_plugin_t* p) { if (auto* e = self (p)->editor.get()) e->setVisible (true); return true; },
            [] (const clap_plugin_t* p) { if (auto* e = self (p)->editor.get()) e->setVisible (false); return true; }
        };
        static const clap_plugin_timer_support_t timer {
            [] (const clap_plugin_t*, clap_id) {
               #if JUCE_LINUX || JUCE_BSD
                for (int i = 0; i < 16 && juce::MessageManager::getInstance()->dispatchNextMessageOnSystemQueue (true); ++i) {}
               #endif
            }
        };

        if (std::strcmp (id, CLAP_EXT_AUDIO_PORTS) == 0) return &audioPorts;
        if (std::strcmp (id, CLAP_EXT_NOTE_PORTS) == 0) return &notePorts;
        if (std::strcmp (id, CLAP_EXT_PARAMS) == 0) return &paramsExt;
        if (std::strcmp (id, CLAP_EXT_LATENCY) == 0) return &latency;
        if (std::strcmp (id, CLAP_EXT_STATE) == 0) return &state;
        if (std::strcmp (id, CLAP_EXT_GUI) == 0) return &gui;
        if (std::strcmp (id, CLAP_EXT_TIMER_SUPPORT) == 0) return &timer;
        return nullptr;
    }

    static const char* nativeApi()
    {
       #if JUCE_WINDOWS
        return CLAP_WINDOW_API_WIN32;
       #else
        return CLAP_WINDOW_API_X11;
       #endif
    }

    void destroyGui()
    {
        if (editor != nullptr)
        {
            processor->editorBeingDeleted (editor.get());
            editor.reset();
        }
    }

    const clap_host_t* host;
    const Entry& entry;
    std::unique_ptr<JuceRuntime> runtime;
    std::unique_ptr<juce::AudioProcessor> processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    juce::Array<juce::AudioProcessorParameter*> parameters;
    std::unique_ptr<std::atomic<bool>[]> pending;
    const clap_host_params_t* hostParams = nullptr;
    const clap_host_latency_t* hostLatency = nullptr;
    const clap_host_state_t* hostState = nullptr;
    const clap_host_timer_support_t* hostTimer = nullptr;
    clap_id timerId = CLAP_INVALID_ID;
    juce::MidiBuffer midi;
    PlayHead position;
};

const clap_plugin_descriptor_t* descriptorOf (std::size_t index)
{
    static const char* const effectFeatures[] { CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_STEREO, nullptr };
    static const char* const synthFeatures[] { CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER, CLAP_PLUGIN_FEATURE_STEREO, nullptr };
    static const auto all = [] {
        std::vector<clap_plugin_descriptor_t> list;
        for (const auto& e : catalog)
        {
            clap_plugin_descriptor_t d {};
            d.clap_version = CLAP_VERSION_INIT;
            d.id = e.clapId;
            d.name = e.name;
            d.vendor = "Ka1-S";
            d.url = "https://lynx-share.com";
            d.manual_url = "";
            d.support_url = "";
            d.version = LYNX_EXPORT_VERSION;
            d.description = "LYNX SHARE plugin";
            d.features = e.instrument ? synthFeatures : effectFeatures;
            list.push_back (d);
        }
        return list;
    }();
    return &all[index];
}

const clap_plugin_factory_t factory {
    [] (const clap_plugin_factory_t*) -> std::uint32_t { return (std::uint32_t) catalog.size(); },
    [] (const clap_plugin_factory_t*, std::uint32_t index) -> const clap_plugin_descriptor_t* {
        return index < catalog.size() ? descriptorOf (index) : nullptr;
    },
    [] (const clap_plugin_factory_t*, const clap_host_t* host, const char* id) -> const clap_plugin_t* {
        for (std::size_t i = 0; i < catalog.size(); ++i)
            if (std::strcmp (id, catalog[i].clapId) == 0)
            {
                return &(new ClapPlugin (host, catalog[i], descriptorOf (i)))->plugin;
            }
        return nullptr;
    }
};
} // namespace
} // namespace lynx::plugexport

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry {
    CLAP_VERSION_INIT,
    [] (const char*) { return true; },
    [] {},
    [] (const char* id) -> const void* {
        return std::strcmp (id, CLAP_PLUGIN_FACTORY_ID) == 0 ? &lynx::plugexport::factory : nullptr;
    }
};
