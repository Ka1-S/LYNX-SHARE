#pragma once

// Export to FL Studio (round 171e, idea by Tookolld): the file side of core/FlpExport — a .flp next to the samples it
// names, or FL's "Zipped loop package": one .zip with the .flp and every sample it uses, stored by file name (FL's own
// packages keep samples by name only, REA_rodada118 §A5), the project naming them by file name.

#include <juce_core/juce_core.h>

#include "core/FlpExport.h"
#include "core/Song.h"
#include "engine/Exporter.h"

namespace lynx
{
class PluginHost;

namespace flstudio
{
struct Written
{
    flpexport::Report report;
    int samples = 0;             // samples put in the package
    juce::StringArray missing;   // samples the project names that are not on this PC
};

// The options for this PC: the plugin list's wrapper records (PluginHost::flWrapperRecord) and the sample lengths.
flpexport::Options optionsFor (PluginHost* host, flpexport::LynxOnly mode);
// The same with every hosted plugin of `song` looked up now (call on the message thread): the options can then be used
// on another thread (they no longer touch the plugin list).
flpexport::Options optionsFor (PluginHost* host, flpexport::LynxOnly mode, const Song& song);

// One render of option A: `song` to `wav` with `settings` (a 32-bit float WAV), `progress` 0..1 (false = cancel).
using RenderFn = std::function<bool (const Song& song, const ExportSettings& settings, const juce::File& wav,
                                     const std::function<bool (double)>& progress, juce::String& error)>;
// The whole export: with option A every channel and insert of flpexport::planRenders is rendered by `render` into
// `renderDir` (the channel / insert name, unique), the renders applied to a copy of `song`, then `target` written.
// `progress` (0..1, what is being done) returns false to cancel. The renders' lines lead written.report.rendered.
bool exportProject (const Song& song, const flpexport::Options& options, const juce::File& target, bool zip,
                    const juce::File& renderDir, double sampleRate, const RenderFn& render,
                    const std::function<bool (double, const juce::String&)>& progress, Written& written, juce::String& error);
// The report as text (the headings in the UI's language when `translate` is given).
juce::String reportText (const Written& written, const std::function<juce::String (const juce::String&)>& translate = {});

// Writes `target` (.flp, or .zip with zip = true) from `song` (option A's renders already applied). False + error on
// failure (the target is only replaced once complete).
bool write (const Song& song, flpexport::Options options, const juce::File& target, bool zip, Written& written, juce::String& error);
} // namespace flstudio
} // namespace lynx
