#pragma once

// JSFX hosting (issue #241, round 190pj; docs/design/JSFX.md stages 1–2): REAPER's text-based effect scripts, run by
// the ysfx library (Apache-2.0, third_party/ysfx). One more juce::AudioPluginFormat next to CLAP and VST2: scanning,
// the plugin list, the Browser, mixer slots, Channel rack generators, the plugin window, saved state, automation and
// delay compensation go through the same paths as the other formats.
//
// - Roots (core/Jsfx.h): Documents/LYNX/JSFX, the bundled pack (written to the settings folder from the executable),
//   REAPER's Effects folder when REAPER is installed, and the folders of Settings › File › JSFX folders.
// - Scan: in this process (a header is only read, nothing is compiled or run); `*.jsfx` files, and extensionless files
//   with a "desc:" line inside the roots (REAPER's own effects).
// - An instance compiles its script when it is made (on the thread that makes it: a big script takes seconds) and runs
//   @init when it is prepared, never on the audio thread unless the script asks for it (transport restart, as REAPER).
// - Sliders are the plugin's parameters (one per slider the script has, in slider order); the slider panel
//   (ui/JsfxPanel.h) is the plugin window's editor. @gfx is not drawn yet (stage 3): those scripts show the panel.
// - Delay compensation: the script's pdc_delay (whole-plugin latency; pdc_bot_ch / pdc_top_ch are not split per channel).
// - MIDI: notes and controls in (with their sample offsets), the script's MIDI out back to LYNX (a generator's MIDI Out
//   port; FL's wrapper Output port).

#include <atomic>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

namespace lynx
{
class JsfxPluginFormat final : public juce::AudioPluginFormat
{
public:
    static constexpr const char* formatName = "JSFX";

    juce::String getName() const override { return formatName; }

    void findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>& results, const juce::String& fileOrIdentifier) override;
    bool fileMightContainThisPluginType (const juce::String& fileOrIdentifier) override;
    juce::String getNameOfPluginFromIdentifier (const juce::String& fileOrIdentifier) override;
    bool pluginNeedsRescanning (const juce::PluginDescription&) override;
    bool doesPluginStillExist (const juce::PluginDescription&) override;
    bool canScanForPlugins() const override { return true; }
    bool isTrivialToScan() const override { return true; } // (PluginHost scans these in its own process)
    // the scripts below these folders (the JSFX roots: PluginHost passes them, not the VST folders)
    juce::StringArray searchPathsForPlugins (const juce::FileSearchPath& directories, bool recursive, bool allowAsync) override;
    juce::FileSearchPath getDefaultLocationsToSearch() override;
    bool requiresUnblockedMessageThreadDuringCreation (const juce::PluginDescription&) const override { return false; }

private:
    void createPluginInstance (const juce::PluginDescription&, double initialSampleRate, int initialBufferSize,
                               PluginCreationCallback) override;
};

namespace jsfx
{
// The JSFX roots, in priority order (a script id found in two roots loads from the first): Documents/LYNX/JSFX, the
// bundled pack, REAPER's Effects folder (when it is there), then the user's folders (Settings › File). Folders that do
// not exist are left out, except the first two (made on first use).
juce::FileSearchPath roots();
// Settings › File › JSFX folders: the folders the user added (jsfx-folders.txt in the settings folder)
juce::StringArray userFolders();
void setUserFolders (const juce::StringArray& folders);
// REAPER's Effects folder of this OS (it may not exist)
juce::File reaperEffectsFolder();
juce::File userFolder();  // Documents/LYNX/JSFX
juce::File packFolder();  // <settings>/JSFX pack
// Writes the bundled pack to packFolder() when it is missing or older than this build's (a hash file inside says which
// pack it is). Cheap when it is current. Any thread; returns the number of files written.
int installPack();

// The file policy (owner decision 1): the folder of the open project, besides the JSFX roots and their Data folders,
// that scripts may read from (message thread; an empty File = none)
void setProjectFolder (const juce::File& folder);
// What the policy allows now (tests): `path` is an existing file
bool mayRead (const juce::File& path);

// A JSFX description: its id relative to its root ("chokehold/Utility/volume_trim.jsfx"), "" for another format
juce::String relativeIdOf (const juce::PluginDescription& description);
// Finds a scanned JSFX by the unique id of another computer's identifier string (core/Jsfx.h: the hash of the relative
// path), when the identifier itself (it carries a hash of the absolute path) is not in `known`
std::unique_ptr<juce::PluginDescription> findPortable (const juce::KnownPluginList& known, const juce::String& identifier);

// What the slider panel shows of a running script (message thread)
struct Slider
{
    int index = 0;           // the slider (0-based: slider1 = 0)
    int parameter = -1;      // its index in getParameters()
    juce::String name;
    bool isEnum = false, isFile = false;
    juce::StringArray choices; // enum names / the file slider's files
    double minimum = 0, maximum = 1, step = 0, defaultValue = 0;
};
class Instance;
Instance* asJsfx (juce::AudioProcessor* plugin);       // nullptr: not a JSFX instance
std::vector<Slider> sliders (Instance& instance);
bool sliderVisible (Instance& instance, int index);    // the script's slider_show / '-' names
double sliderValue (Instance& instance, int index);    // the plugin's own value (not normalised)
void setSliderValue (Instance& instance, int index, double value); // as the user's edit (notifies the host)
juce::String scriptName (Instance& instance);
juce::String scriptAuthor (Instance& instance);
juce::String scriptFile (Instance& instance);
juce::String relativeId (Instance& instance);
bool hasGfx (Instance& instance);       // the script draws its own UI (@gfx; not shown before stage 3)
bool scriptChanged (Instance& instance); // the state loaded was saved with another version of the script
int latency (Instance& instance);       // the script's pdc_delay now (samples)

// --selftest: the format end to end with a bundled script (reference render against ysfx's own output, state round
// trip, PDC, MIDI in / out, file policy, parameters, the slot in PluginEffect / PluginInstrument). "OK ..." or "ERROR ...".
juce::String selfTest (const juce::File& reportFolder);
// --jsfxbench <out.txt>: CPU per instance of every bundled script (ns per frame, stereo 48 kHz, 512-frame blocks) next
// to a plain copy loop
juce::String bench (const juce::File& output);
} // namespace jsfx
} // namespace lynx
