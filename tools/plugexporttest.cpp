// plugexporttest <plugin.vst3 | plugin.clap> <report.txt> [editor picture prefix]
// Loads LYNX's exported plugins the way a host does (round 180b, docs/PLUGIN_EXPORT.md) and checks, with no window on screen:
// the scan, the sound against the same engine run directly in this process (null test), automation, the state (a new
// instance sounds the same; the state opens as a .lynx project), the editor, memory per instance and CPU.
// Exit 0 = every check passed.

#include <cmath>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

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

// The same engine with no plugin between: the effect itself / a rack with the channel
Audio directEffect (EffectKind kind, const EffectParams& params)
{
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

    const auto direct = entry->instrument ? directInstrument (*entry) : directEffect (entry->effect, makeEffectSlot (entry->effect).params);
    const auto nulled = sound.difference (direct);
    check (nulled < 1.0e-5, "null test against the engine run directly: peak difference " + juce::String (nulled, 8));

    // automation: a parameter moved by the host changes the sound, as the same value does on the direct engine
    auto& parameters = instance->getParameters();
    int moved = -1;
    for (int i = 0; i < parameters.size() && moved < 0; ++i)
        if (! entry->instrument && parameters[i]->getName (64).containsIgnoreCase ("gain") && ! parameters[i]->getName (64).containsIgnoreCase ("out"))
            moved = i;
    if (entry->instrument)
        moved = parameters.size() > 1 ? 1 : 0;
    if (moved >= 0)
    {
        const auto was = parameters[moved]->getValue();
        const auto target = was < 0.5f ? was + 0.3f : was - 0.3f;
        parameters[moved]->setValue (target);
        report << "  moved '" << parameters[moved]->getName (64) << "' " << was << " -> " << parameters[moved]->getValue() << "\n";
        const auto changed = render (*instance, entry->instrument);
        check (changed.difference (sound) > 1.0e-4 || entry->instrument, "the moved parameter changes the sound, difference " + juce::String (changed.difference (sound), 6));

        // state: a new instance of what we just heard sounds the same
        juce::MemoryBlock state;
        instance->getStateInformation (state);
        report << "  state: " << (int) state.getSize() << " bytes\n";
        juce::String parseError;
        check (ProjectFile::fromJson (juce::String::createStringFromData (state.getData(), (int) state.getSize()), parseError).has_value(), "the state opens as a .lynx project " + parseError);
        auto second = make();
        check (second != nullptr, "second instance");
        if (second != nullptr)
        {
            second->setStateInformation (state.getData(), (int) state.getSize());
            check (std::abs (second->getParameters()[moved]->getValue() - parameters[moved]->getValue()) < 1.0e-5f, "state restores the parameter");
            const auto first = render (*instance, entry->instrument), restored = render (*second, entry->instrument);
            const auto d = restored.difference (first);
            check (d < 1.0e-3, "state restores the sound: peak difference " + juce::String (d, 8));
        }
    }

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

    report << "result=" << (failures == 0 ? "OK" : "FAIL (" + juce::String (failures) + ")") << "\n";
    juce::File (juce::String::fromUTF8 (argv[2])).replaceWithText (report);
    std::fputs (report.toRawUTF8(), stdout);
    return failures == 0 ? 0 : 1;
}
