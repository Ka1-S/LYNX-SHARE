// LYNX-bridge32.exe (round 176a): the 32-bit plugin bridge. LYNX is a 64-bit program, and a 64-bit process cannot load
// a 32-bit DLL, so LYNX runs each 32-bit VST2 / VST3 plugin in one of these processes — the same child side as "Make
// bridged" (engine/SandboxWorker.cpp: shared-memory audio, MIDI, parameters and transport every block; state and the
// editor window through messages), compiled for x86. FL Studio does the same with System\Tools\Bridge\32bit\ilbridge.exe.
//
//   LYNX-bridge32.exe --plugin-host ...                      (started by LYNX: engine/PluginSandbox.cpp)
//   LYNX-bridge32.exe --scan-one <format> <file> <result.xml>  (the plugin scan, PluginHost::scanOne)
//
// It has no window, audio device or settings of its own, and ends when LYNX goes away. A plugin that crashes takes only
// this process down: LYNX mutes the plugin and says so.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include "engine/PluginSandbox.h"
#include "engine/SandboxWorker.h"
#include "engine/Vst2Format.h"

#include <windows.h>

namespace lynx
{
namespace
{
void addFormats (juce::AudioPluginFormatManager& formats)
{
    formats.addFormat (new Vst2PluginFormat());
    formats.addFormat (new juce::VST3PluginFormat());
}
} // namespace

namespace sandboxhost
{
std::unique_ptr<juce::AudioPluginInstance> open (const juce::PluginDescription& description, double sampleRate, int blockSize, juce::String& error)
{
    juce::AudioPluginFormatManager formats;
    addFormats (formats);
    return formats.createPluginInstance (description, sampleRate, blockSize, error);
}

// PluginHost.cpp's preparePlugin
void prepare (juce::AudioPluginInstance& plugin, double sampleRate, int blockSize)
{
    plugin.enableAllBuses();
    auto layout = plugin.getBusesLayout();
    if (! layout.outputBuses.isEmpty())
    {
        layout.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
        if (! layout.inputBuses.isEmpty())
            layout.inputBuses.getReference (0) = juce::AudioChannelSet::stereo();
        if (plugin.checkBusesLayoutSupported (layout))
            plugin.setBusesLayout (layout);
    }
    plugin.setRateAndBufferSizeDetails (sampleRate, blockSize);
    plugin.prepareToPlay (sampleRate, blockSize);
}

std::string captureState (juce::AudioPluginInstance& plugin)
{
    juce::MemoryBlock state;
    plugin.getStateInformation (state);
    return state.toBase64Encoding().toStdString();
}

juce::Colour windowColour() { return juce::Colour (0xff1b1d22); } // (theme::colour::bg1 of the default dark theme)
juce::Image windowIcon() { return {}; }

// FL's bridge runs plugins with x87 control word 0x033F: all exceptions masked, 64-bit precision, round to nearest
// (ilbridge.exe 0x528ba0 / constant 0x71b368, REA_rodada68 §2.1). An old plugin doing its maths on the x87 then gives
// the same samples in LYNX as in FL (Windows' own default for a 32-bit program is 0x027F, 53-bit precision).
// LYNX_BRIDGE32_X87=0 leaves the default (A/B tests).
unsigned x87ControlWord()
{
    static const unsigned word = juce::SystemStats::getEnvironmentVariable ("LYNX_BRIDGE32_X87", "033F").getHexValue32();
    return word;
}
} // namespace sandboxhost

namespace
{
// PluginHost::scanOneFile in the bridge: describes a 32-bit module's plugins into `output` (XML)
int scanOneFile (const juce::String& formatName, const juce::String& file, const juce::File& output)
{
    juce::AudioPluginFormatManager manager;
    addFormats (manager);
    juce::OwnedArray<juce::PluginDescription> found;
    for (auto* format : manager.getFormats())
        if (format->getName() == formatName)
            format->findAllTypesForFile (found, file);
    juce::XmlElement xml ("PLUGINS");
    for (auto* d : found)
        xml.addChildElement (d->createXml().release());
    xml.writeTo (output);
    return found.size();
}
} // namespace

class Bridge32Application final : public juce::JUCEApplicationBase
{
public:
    const juce::String getApplicationName() override { return "LYNX bridge (32-bit)"; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return true; }
    void anotherInstanceStarted (const juce::String&) override {}
    void suspended() override {}
    void resumed() override {}
    void systemRequestedQuit() override { quit(); }
    void unhandledException (const std::exception*, const juce::String&, int) override {}

    void initialise (const juce::String& commandLine) override
    {
        // no Windows error boxes for a plugin that crashes or a DLL that is missing: LYNX reports it
        SetErrorMode (SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
        const auto args = juce::StringArray::fromTokens (commandLine, true);
        if (const auto i = args.indexOf ("--scan-one"); i >= 0)
        {
            const auto found = scanOneFile (args[i + 1].unquoted(), args[i + 2].unquoted(), juce::File (args[i + 3].unquoted()));
            // the answer is written: end without unloading the plugin (its teardown may hang; PluginHost::scanOneFile)
            juce::ignoreUnused (found);
            TerminateProcess (GetCurrentProcess(), 0);
        }
        if (! runPluginSandboxWorker (commandLine))
        {
            setApplicationReturnValue (2);
            quit();
        }
    }

    void shutdown() override
    {
        shutdownPluginSandboxWorker();
        // plugins stay loaded to the end (some crash when unloaded); end without their DLL teardown
        TerminateProcess (GetCurrentProcess(), 0);
    }
};
} // namespace lynx

START_JUCE_APPLICATION (lynx::Bridge32Application)
