#include "ui/RemoteControlSettings.h"
#include "platform/Paths.h"
#include <optional>

namespace lynx::remoteui
{
namespace
{
std::optional<bool> requested;
bool running = false;
juce::String detail;
std::function<void (bool)> change;
juce::File settingsFile() { return platform::settingsFile ("remote-control.txt"); }
}
bool enabled()
{
    if (! requested)
    {
        const auto forced = juce::SystemStats::getEnvironmentVariable ("LYNX_CONTROL_ENABLED", {}).trim();
        requested = forced.isNotEmpty() ? forced == "1" : settingsFile().loadFileAsString().trim() == "1";
    }
    return *requested;
}
bool active() { return running; }
juce::String status()
{
    if (detail.isNotEmpty()) return detail;
    return running ? "Remote control active — local clients can read and edit this project."
                   : enabled() ? "Remote control could not start. Disable and re-enable to retry."
                               : "Remote control is off.";
}
void setEnabled (bool on)
{
    if (on == enabled()) return;
    if (! settingsFile().replaceWithText (on ? "1\n" : "0\n"))
    {
        if (! on)
        {
            requested = false;
            if (change) change (false);
        }
        detail = "The remote control setting could not be saved.";
        return;
    }
    requested = on;
    detail = {};
    if (change) change (on);
}
void setRuntimeStatus (bool on, const juce::String& message)
{
    running = on;
    detail = message;
}
void install (std::function<void (bool)> enable)
{
    change = std::move (enable);
    if (change) change (enabled());
}
void shutdown()
{
    change = {};
    running = false;
}
}
