// plugexporttest <plugin.vst3 | plugin.clap> <report.txt> [editor picture prefix]
// Loads LYNX's exported plugins the way a host does (round 180b, docs/PLUGIN_EXPORT.md) and checks, with no window on screen:
// the scan, the sound against the same engine run directly in this process (null test), automation, the state (a new
// instance sounds the same; the state opens as a .lynx project), the editor, memory per instance and CPU.
// Exit 0 = every check passed.

#include <atomic>
#include <cmath>
#include <cstring>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <clap/clap.h>

#include "core/Effects.h"
#include "core/LynxBanks.h"
#include "core/RackRenderer.h"
#include "engine/ClapFormat.h"
#include "engine/ProjectFile.h"
#include "plugin/ExportProcessor.h"

#if JUCE_WINDOWS
 #include <windows.h>
 #include <psapi.h>
#endif

using namespace lynx;
using lynx::plugexport::Entry;

namespace
{
constexpr double rate = 48000.0;
constexpr int block = 512, blocks = 200;

juce::String report;
int failures = 0;

void check (bool ok, const juce::String& what)
{
    report << (ok ? "PASS " : "FAIL ") << what << "\n";
    failures += ok ? 0 : 1;
}

double memoryMb()
{
   #if JUCE_WINDOWS
    PROCESS_MEMORY_COUNTERS c {};
    GetProcessMemoryInfo (GetCurrentProcess(), &c, sizeof (c));
    return (double) c.WorkingSetSize / 1048576.0;
   #else
    const auto fields = juce::StringArray::fromTokens (juce::File ("/proc/self/statm").loadFileAsString(), " ", "");
    return fields[1].getDoubleValue() * 4096.0 / 1048576.0;
   #endif
}

struct Audio
{
    std::vector<float> l, r;
    double peak() const
    {
        double p = 0.0;
        for (auto v : l) p = std::max (p, (double) std::abs (v));
        for (auto v : r) p = std::max (p, (double) std::abs (v));
        return p;
    }
    // the first sample where the two differ by more than `tolerance` (-1 = none)
    long firstDifference (const Audio& o, double tolerance = 1.0e-5) const
    {
        for (std::size_t i = 0; i < std::min (l.size(), o.l.size()); ++i)
            if (std::abs (l[i] - o.l[i]) > tolerance || std::abs (r[i] - o.r[i]) > tolerance)
                return (long) i;
        return -1;
    }
    double difference (const Audio& o) const
    {
        double p = l.size() == o.l.size() ? 0.0 : 1e9;
        for (std::size_t i = 0; i < std::min (l.size(), o.l.size()); ++i)
            p = std::max ({ p, (double) std::abs (l[i] - o.l[i]), (double) std::abs (r[i] - o.r[i]) });
        return p;
    }
};

// test input: a decaying noise / sine mix (deterministic)
void fillInput (float* l, float* r, int n, int start)
{
    Noise noise;
    noise.state = 0x1234567u + (std::uint32_t) start;
    for (int i = 0; i < n; ++i)
    {
        const auto t = (double) (start + i) / rate;
        const auto v = 0.2f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * t) + 0.1f * noise.next();
        l[i] = v;
        r[i] = 0.7f * v;
    }
}

juce::MidiBuffer notesFor (int b)
{
    juce::MidiBuffer m;
    if (b == 2) m.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 100);
    if (b == 2) m.addEvent (juce::MidiMessage::noteOn (1, 67, 0.6f), 300);
    if (b == 100) m.addEvent (juce::MidiMessage::noteOff (1, 60), 50);
    if (b == 100) m.addEvent (juce::MidiMessage::noteOff (1, 67), 50);
    return m;
}

Audio render (juce::AudioProcessor& p, bool instrument, double* seconds = nullptr)
{
    Audio out;
    p.setPlayConfigDetails (instrument ? 0 : 2, 2, rate, block);
    p.prepareToPlay (rate, block);
    juce::AudioBuffer<float> buffer (2, block);
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    for (int b = 0; b < blocks; ++b)
    {
        buffer.clear();
        if (! instrument)
            fillInput (buffer.getWritePointer (0), buffer.getWritePointer (1), block, b * block);
        auto midi = instrument ? notesFor (b) : juce::MidiBuffer();
        p.processBlock (buffer, midi);
        out.l.insert (out.l.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + block);
        out.r.insert (out.r.end(), buffer.getReadPointer (1), buffer.getReadPointer (1) + block);
    }
    if (seconds != nullptr)
        *seconds = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
    p.releaseResources();
    return out;
}

// Round 188b: an effect with its side-chain bus switched on (stereo), the side fed with `side` × a square wave; the main
// input as in render(). Empty: the host couldn't switch the bus on.
Audio renderWithSide (juce::AudioProcessor& p, float side)
{
    Audio out;
    auto layout = p.getBusesLayout();
    if (layout.inputBuses.size() < 2)
        return out;
    layout.inputBuses.getReference (1) = juce::AudioChannelSet::stereo();
    if (! p.setBusesLayout (layout))
        return out;
    p.setRateAndBufferSizeDetails (rate, block);
    p.prepareToPlay (rate, block);
    const auto channels = juce::jmax (p.getTotalNumInputChannels(), p.getTotalNumOutputChannels());
    juce::AudioBuffer<float> buffer (channels, block);
    for (int b = 0; b < blocks; ++b)
    {
        buffer.clear();
        fillInput (buffer.getWritePointer (0), buffer.getWritePointer (1), block, b * block);
        for (int c = 2; c < juce::jmin (4, channels); ++c)
            for (int i = 0; i < block; ++i)
                buffer.setSample (c, i, side * (i % 64 < 32 ? 1.0f : -1.0f));
        juce::MidiBuffer midi;
        p.processBlock (buffer, midi);
        out.l.insert (out.l.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + block);
        out.r.insert (out.r.end(), buffer.getReadPointer (1), buffer.getReadPointer (1) + block);
    }
    p.releaseResources();
    // (back to the main buses only, as render() expects)
    layout.inputBuses.getReference (1) = juce::AudioChannelSet::disabled();
    p.setBusesLayout (layout);
    return out;
}

// The same engine with no plugin between: the effect itself / a rack with the channel
Audio directEffect (EffectKind kind, const EffectParams& params)
{
    const juce::ScopedNoDenormals noDenormals; // (as the plugin's processBlock: denormals flushed)
    Audio out;
    auto effect = makeEffect (kind);
    effect->prepare (rate);
    std::vector<float> l ((std::size_t) block), r ((std::size_t) block);
    for (int b = 0; b < blocks; ++b)
    {
        fillInput (l.data(), r.data(), block, b * block);
        effect->setSongPosition (0.0);
        effect->setPlaying (false);
        effect->process (l.data(), r.data(), block, params, 120.0);
        out.l.insert (out.l.end(), l.begin(), l.end());
        out.r.insert (out.r.end(), r.begin(), r.end());
    }
    return out;
}

Audio directInstrument (const Entry& entry)
{
    Audio out;
    auto song = Song::makeDefault();
    song.channels.clear();
    song.mixer.resize (2);
    song.mixer[0].effects.clear();
    song.mixer[1].effects.clear();
    auto channel = banks::defaultChannel (entry.synth, entry.name);
    channel->mixerTrack = 0;
    song.channels.push_back (*channel);
    auto state = RackState::build (song, nullptr, rate);
    auto renderer = std::make_unique<RackRenderer>();
    renderer->prepare (rate, block);
    LiveEventQueue queue;
    std::vector<float> l ((std::size_t) block), r ((std::size_t) block);
    for (int b = 0; b < blocks; ++b)
    {
        auto done = 0;
        const auto upTo = [&] (int end) {
            if (end > done)
                renderer->process (state.get(), false, 0.0, end - done, l.data() + done, r.data() + done, &queue);
            done = std::max (done, end);
        };
        for (const auto m : notesFor (b))
        {
            upTo (m.samplePosition);
            const auto message = m.getMessage();
            NoteEvent e;
            e.channel = 0;
            e.key = message.getNoteNumber();
            e.type = message.isNoteOn() ? NoteEvent::Type::on : NoteEvent::Type::off;
            e.velocity = message.isNoteOn() ? message.getFloatVelocity() : 0.0f;
            queue.push (e);
        }
        upTo (block);
        out.l.insert (out.l.end(), l.begin(), l.end());
        out.r.insert (out.r.end(), r.begin(), r.end());
    }
    return out;
}

void writeEditorPicture (const Entry& entry, const juce::String& prefix)
{
    std::unique_ptr<juce::AudioProcessor> p (plugexport::makeProcessor (entry));
    {
        juce::MemoryBlock state; // (in-process: a VST3 host sees it inside JUCE's own chunk)
        p->getStateInformation (state);
        juce::String parseError;
        check (ProjectFile::fromJson (juce::String::createStringFromData (state.getData(), (int) state.getSize()), parseError).has_value(),
               "the state opens as a .lynx project " + parseError);
    }
    const auto before = memoryMb();
    std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditorIfNeeded());
    check (editor != nullptr && editor->getWidth() > 100 && editor->getHeight() > 50,
           juce::String ("in-process editor of ") + entry.name + " " + juce::String (editor != nullptr ? editor->getWidth() : 0) + "x" + juce::String (editor != nullptr ? editor->getHeight() : 0));
    report << "  editor open adds " << juce::String (memoryMb() - before, 1) << " MB\n";
    if (editor == nullptr)
        return;
    juce::Image image (juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);
    {
        juce::Graphics g (image);
        editor->paintEntireComponent (g, false);
    }
    const juce::File file (prefix + juce::String (entry.name).removeCharacters (" ") + ".png");
    file.deleteFile();
    juce::PNGImageFormat png;
    if (auto stream = file.createOutputStream())
        png.writeImageToStream (image, *stream);
    p->editorBeingDeleted (editor.get());
    editor.reset();
}

} // namespace
#if JUCE_MAC
 #include <CoreFoundation/CoreFoundation.h>
#else
namespace juce::detail { bool dispatchNextMessageOnSystemQueue (bool returnIfNoPendingMessages); } // (juce_events' native code)
#endif
namespace
{
// Lets the message loop (and a CLAP plugin's timer) run for a while
void pump (int ms, const std::function<void()>& tick = {})
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
    while (juce::Time::getMillisecondCounter() < end)
    {
       #if JUCE_MAC
        CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.005, false); // (JUCE's messages come through the main run loop)
       #else
        while (juce::detail::dispatchNextMessageOnSystemQueue (true)) {}
        juce::Thread::sleep (5);
       #endif
        if (tick)
            tick();
    }
}

// Counts what a host would take as an edit: parameter changes coming from the plugin
struct EditCounter final : juce::AudioProcessorParameter::Listener
{
    std::atomic<int> changes { 0 };
    void parameterValueChanged (int, float) override { ++changes; }
    void parameterGestureChanged (int, bool) override { ++changes; }
};

// A loaded state (a project being opened) is not an edit: the host must get no parameter changes from it, at once or
// later from the editor's sync (round 180b: FL Studio asked to save a project it had only opened)
void stateLoadIsSilent (juce::AudioPluginFormat& format, const juce::PluginDescription& description, const juce::MemoryBlock& state)
{
    juce::String error;
    auto third = format.createInstanceFromDescription (description, rate, block, error);
    if (third == nullptr)
        return;
    third->prepareToPlay (rate, block);
    EditCounter counter;
    for (auto* p : third->getParameters())
        p->addListener (&counter);
    third->setStateInformation (state.getData(), (int) state.getSize());
    // (JUCE's VST3 host re-reads every value inside setStateInformation and tells its own listeners: that is the host
    // updating its view, not an edit from the plugin, whose wrapper sends none while a state loads; what comes later is)
    report << "  host refreshed " << counter.changes.exchange (0) << " values inside the state load\n";
    pump (300);
    const auto afterLoad = counter.changes.load();
    std::unique_ptr<juce::AudioProcessorEditor> editor (third->createEditorIfNeeded());
    pump (600);
    const auto withEditor = counter.changes.load() - afterLoad;
    editor.reset();
    for (auto* p : third->getParameters())
        p->removeListener (&counter);
    check (afterLoad == 0 && withEditor == 0, "a loaded state sends the host no edits: " + juce::String (afterLoad)
                                                  + " at load, " + juce::String (withEditor) + " with the editor open");
}

// The same for the CLAP, with a host of our own that sees what LYNX's CLAP host ignores: parameter events the plugin
// sends out and host_state.mark_dirty ("project modified")
struct RawClapHost
{
    static inline std::atomic<int> dirty { 0 }, flushes { 0 };
    static inline clap_id timer = CLAP_INVALID_ID;
    clap_host_t host {};
    clap_host_params_t params { [] (const clap_host_t*, clap_param_rescan_flags) {}, [] (const clap_host_t*, clap_id, clap_param_clear_flags) {},
                                [] (const clap_host_t*) { ++flushes; } };
    clap_host_state_t state { [] (const clap_host_t*) { ++dirty; } };
    clap_host_latency_t latency { [] (const clap_host_t*) {} };
    clap_host_timer_support_t timers { [] (const clap_host_t*, uint32_t, clap_id* id) { *id = timer = 1; return true; },
                                       [] (const clap_host_t*, clap_id) { timer = CLAP_INVALID_ID; return true; } };
    RawClapHost()
    {
        host.clap_version = CLAP_VERSION;
        host.host_data = this;
        host.name = "plugexporttest";
        host.vendor = "LYNX";
        host.url = "";
        host.version = "1";
        host.get_extension = [] (const clap_host_t* h, const char* id) -> const void* {
            auto* self = static_cast<RawClapHost*> (h->host_data);
            if (std::strcmp (id, CLAP_EXT_PARAMS) == 0) return &self->params;
            if (std::strcmp (id, CLAP_EXT_STATE) == 0) return &self->state;
            if (std::strcmp (id, CLAP_EXT_LATENCY) == 0) return &self->latency;
            if (std::strcmp (id, CLAP_EXT_TIMER_SUPPORT) == 0) return &self->timers;
            return nullptr;
        };
        host.request_restart = [] (const clap_host_t*) {};
        host.request_process = [] (const clap_host_t*) {};
        host.request_callback = [] (const clap_host_t*) {};
    }
};

void clapStateLoadIsSilent (const juce::File& file, const Entry& entry)
{
    // a state with moved parameters, from the same engine in this process
    std::unique_ptr<juce::AudioProcessor> source (plugexport::makeProcessor (entry));
    for (auto* p : source->getParameters())
        p->setValueNotifyingHost (std::fmod (p->getDefaultValue() + 0.37f, 1.0f));
    juce::MemoryBlock saved;
    source->getStateInformation (saved);

    juce::DynamicLibrary library;
    auto binary = file; // (macOS: the executable inside the .clap bundle; clap_entry.init still gets the bundle)
    if (file.isDirectory())
        if (const auto inside = file.getChildFile ("Contents/MacOS").getChildFile (file.getFileNameWithoutExtension()); inside.existsAsFile())
            binary = inside;
    if (! library.open (binary.getFullPathName()))
        return check (false, "CLAP opens in a plain host");
    const auto* clapEntry = static_cast<const clap_plugin_entry_t*> (library.getFunction ("clap_entry"));
    if (clapEntry == nullptr || ! clapEntry->init (file.getFullPathName().toRawUTF8()))
        return check (false, "CLAP entry in a plain host");
    {
        RawClapHost host;
        const auto* factory = static_cast<const clap_plugin_factory_t*> (clapEntry->get_factory (CLAP_PLUGIN_FACTORY_ID));
        const auto* plugin = factory != nullptr ? factory->create_plugin (factory, &host.host, entry.clapId) : nullptr;
        if (plugin == nullptr || ! plugin->init (plugin))
            check (false, "CLAP instance in a plain host");
        else
        {
            const auto* state = static_cast<const clap_plugin_state_t*> (plugin->get_extension (plugin, CLAP_EXT_STATE));
            const auto* params = static_cast<const clap_plugin_params_t*> (plugin->get_extension (plugin, CLAP_EXT_PARAMS));
            const auto* gui = static_cast<const clap_plugin_gui_t*> (plugin->get_extension (plugin, CLAP_EXT_GUI));
            const auto* timer = static_cast<const clap_plugin_timer_support_t*> (plugin->get_extension (plugin, CLAP_EXT_TIMER_SUPPORT));
            int sentOut = 0;
            clap_input_events_t none { nullptr, [] (const clap_input_events_t*) -> uint32_t { return 0; },
                                       [] (const clap_input_events_t*, uint32_t) -> const clap_event_header_t* { return nullptr; } };
            clap_output_events_t out { &sentOut, [] (const clap_output_events_t* o, const clap_event_header_t*) { ++*static_cast<int*> (o->ctx); return true; } };
            auto tick = [&] {
                if (timer != nullptr && RawClapHost::timer != CLAP_INVALID_ID)
                    timer->on_timer (plugin, RawClapHost::timer);
                if (params != nullptr)
                    params->flush (plugin, &none, &out);
            };
            struct Reader { const juce::MemoryBlock* data; std::size_t at = 0; } reader { &saved };
            clap_istream_t in { &reader, [] (const clap_istream_t* s, void* buffer, uint64_t size) -> int64_t {
                auto* r = static_cast<Reader*> (s->ctx);
                const auto n = std::min<std::size_t> ((std::size_t) size, r->data->getSize() - r->at);
                std::memcpy (buffer, static_cast<const char*> (r->data->getData()) + r->at, n);
                r->at += n;
                return (int64_t) n;
            } };
            RawClapHost::dirty = 0;
            const auto loaded = state != nullptr && state->load (plugin, &in);
            check (loaded, "CLAP state loads in a plain host");
            pump (300, tick);
            const auto atLoad = RawClapHost::dirty.load() + sentOut;
            int withEditor = 0;
           #if JUCE_WINDOWS
            const char* api = CLAP_WINDOW_API_WIN32;
           #else
            const char* api = CLAP_WINDOW_API_X11;
           #endif
            if (gui != nullptr && gui->create (plugin, api, false))
            {
                const auto before = RawClapHost::dirty.load() + sentOut;
                pump (600, tick);
                withEditor = RawClapHost::dirty.load() + sentOut - before;
                gui->destroy (plugin);
            }
            check (atLoad == 0 && withEditor == 0, "a loaded CLAP state marks nothing dirty and sends no parameter events: "
                                                       + juce::String (atLoad) + " at load, " + juce::String (withEditor) + " with the editor open");
            plugin->destroy (plugin);
        }
    }
    clapEntry->deinit();
}

const Entry* entryNamed (const juce::String& name)
{
    for (const auto& e : plugexport::catalog)
        if (name == e.name)
            return &e;
    return nullptr;
}

void testPlugin (juce::AudioPluginFormat& format, const juce::PluginDescription& description, const juce::String& picturePrefix)
{
    const auto* entry = entryNamed (description.name);
    check (entry != nullptr, "known name: " + description.name);
    if (entry == nullptr)
        return;
    report << "--- " << description.name << " (" << format.getName() << ", " << (description.isInstrument ? "instrument" : "effect") << ")\n";
    check (description.isInstrument == entry->instrument, "scan says instrument or effect correctly");

    juce::String error;
    const auto base = memoryMb();
    auto make = [&] { return format.createInstanceFromDescription (description, rate, block, error); };
    auto instance = make();
    check (instance != nullptr, "instance " + error);
    if (instance == nullptr)
        return;
    report << "  first instance: +" << juce::String (memoryMb() - base, 1) << " MB\n";
    check (instance->getParameters().size() > 0, "parameters: " + juce::String (instance->getParameters().size()));

    double seconds = 0.0;
    const auto sound = render (*instance, entry->instrument, &seconds);
    report << "  " << blocks << " blocks of " << block << ": " << juce::String (100.0 * seconds / (blocks * block / rate), 2) << " % of real time\n";
    check (sound.peak() > 1.0e-3, "makes sound, peak " + juce::String (sound.peak(), 4));

    auto direct = entry->instrument ? directInstrument (*entry) : directEffect (entry->effect, makeEffectSlot (entry->effect).params);
    const auto nulled = sound.difference (direct);
    check (nulled < 1.0e-5, "null test at the defaults: peak difference " + juce::String (nulled, 8));

    // round 188b: the side-chain bus. Switched on and silent, nothing changes (at the defaults no effect listens to it)
    if (! entry->instrument)
    {
        const auto withSide = renderWithSide (*instance, 0.0f);
        if (withSide.l.empty())
            report << "  side-chain bus: this host can't switch it on\n";
        else
            check (withSide.difference (sound) < 1.0e-5, "side-chain bus on and silent: peak difference " + juce::String (withSide.difference (sound), 8));
        if (entry->effect == EffectKind::crestCompressor && ! withSide.l.empty())
        {
            // its external side chain on: a loud side signal compresses the main input
            const auto& names = effectInfo (entry->effect).params;
            for (int i = 0; i < (int) names.size() && i < instance->getParameters().size(); ++i)
                if (juce::String::fromUTF8 (names[(std::size_t) i].name) == "External SC")
                    instance->getParameters()[i]->setValue (1.0f);
            const auto quiet = renderWithSide (*instance, 0.0f), loud = renderWithSide (*instance, 0.9f);
            const auto energy = [] (const Audio& a) { double e = 0.0; for (auto v : a.l) e += (double) v * v; return e; };
            check (energy (loud) < 0.7 * energy (quiet), "external side chain through the bus compresses: " + juce::String (energy (loud) / juce::jmax (1.0e-12, energy (quiet)), 3));
            for (int i = 0; i < (int) names.size() && i < instance->getParameters().size(); ++i)
                if (juce::String::fromUTF8 (names[(std::size_t) i].name) == "External SC")
                    instance->getParameters()[i]->setValue (0.0f);
        }
    }

    auto& parameters = instance->getParameters();
    if (! entry->instrument)
    {
        // every named parameter moved through the host, against the same values on the engine run directly. Facet EQ's
        // defaults are flat (its output is its input), so this is the null test that says something.
        auto params = makeEffectSlot (entry->effect).params;
        const auto named = (int) effectInfo (entry->effect).params.size();
        for (int i = 0; i < named && i < parameters.size(); ++i)
        {
            const auto v = 0.5f + 0.45f * (float) std::sin ((double) i * 0.7);
            parameters[i]->setValue (v);
            params[(std::size_t) i] = parameters[i]->getValue();
        }
        const auto wet = render (*instance, false);
        const auto d = wet.difference (direct = directEffect (entry->effect, params));
        check (d < 1.0e-5, "null test with " + juce::String (named) + " parameters moved by the host: peak difference " + juce::String (d, 8)
                               + (d < 1.0e-5 ? juce::String() : ", first at sample " + juce::String (wet.firstDifference (direct))));
        check (wet.difference (sound) > 1.0e-3, "those settings change the sound, difference " + juce::String (wet.difference (sound), 5));
    }
    else
    {
        const auto was = parameters[1]->getValue();
        parameters[1]->setValue (was < 0.5f ? was + 0.3f : was - 0.3f);
        report << "  moved '" << parameters[1]->getName (64) << "' " << was << " -> " << parameters[1]->getValue() << "\n";
        const auto changed = render (*instance, true);
        check (changed.difference (sound) > 1.0e-3, "the moved parameter changes the sound, difference " + juce::String (changed.difference (sound), 5));
    }

    // state: a new instance of what we just heard sounds the same
    juce::MemoryBlock state;
    instance->getStateInformation (state);
    report << "  state: " << (int) state.getSize() << " bytes\n";
    auto second = make();
    check (second != nullptr, "second instance");
    if (second != nullptr)
    {
        second->setStateInformation (state.getData(), (int) state.getSize());
        const auto first = render (*instance, entry->instrument), restored = render (*second, entry->instrument);
        const auto d = restored.difference (first);
        check (d < 1.0e-3, "state restores the sound: peak difference " + juce::String (d, 8)
                               + (d < 1.0e-3 ? juce::String() : ", first at sample " + juce::String (restored.firstDifference (first, 1.0e-3))));
    }
    stateLoadIsSilent (format, description, state);

    // hosted editor object (never put on the screen)
    if (auto* editor = instance->createEditorIfNeeded())
    {
        check (editor->getWidth() > 100, "hosted editor created, " + juce::String (editor->getWidth()) + "x" + juce::String (editor->getHeight()));
        delete editor;
    }
    else
        check (false, "hosted editor created");

    const auto oneMore = memoryMb();
    std::vector<std::unique_ptr<juce::AudioPluginInstance>> many;
    for (int i = 0; i < 8; ++i)
        many.push_back (make());
    report << "  8 more instances: +" << juce::String ((memoryMb() - oneMore) / 8.0, 1) << " MB each\n";
    many.clear();
    instance.reset();

    if (picturePrefix.isNotEmpty())
        writeEditorPicture (*entry, picturePrefix);
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 3)
        return 2;
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File plugin (juce::String::fromUTF8 (argv[1]));
    const juce::String picturePrefix = argc > 3 ? juce::String::fromUTF8 (argv[3]) : juce::String();

    juce::VST3PluginFormat vst3;
    ClapPluginFormat clap;
    juce::AudioPluginFormat& format = plugin.hasFileExtension ("clap") ? static_cast<juce::AudioPluginFormat&> (clap) : vst3;
    juce::OwnedArray<juce::PluginDescription> found;
    format.findAllTypesForFile (found, plugin.getFullPathName());
    report << "plugin " << plugin.getFileName() << ": " << found.size() << " found\n";
    check (found.size() > 0, "scan finds plugins");
    for (auto* d : found)
        testPlugin (format, *d, picturePrefix);
    if (plugin.hasFileExtension ("clap"))
        for (auto* d : found)
            if (const auto* entry = entryNamed (d->name))
                clapStateLoadIsSilent (plugin, *entry);

    report << "result=" << (failures == 0 ? "OK" : "FAIL (" + juce::String (failures) + ")") << "\n";
    juce::File (juce::String::fromUTF8 (argv[2])).replaceWithText (report);
    std::fputs (report.toRawUTF8(), stdout);
    return failures == 0 ? 0 : 1;
}
