#pragma once

#include <functional>
#include <memory>
#include <juce_core/juce_core.h>

namespace lynx
{
class AudioEngine;

// Opt-in, vendor-neutral local control. Disabled means no listener or discovery
// file. The engine and callbacks must outlive this object.
class RemoteControl final
{
public:
    static std::unique_ptr<RemoteControl> create (AudioEngine&, bool enabled, std::function<juce::String()> projectName,
                                                 std::function<juce::String()> projectPath = {});
    ~RemoteControl();
    bool active() const noexcept;
    juce::File discoveryFile() const;

    // Round 190np (LYNX --demo, ui/DemoMode.h): more operations, run by `handler` on the message thread like the others
    // (view operations of the window: they take no project guards). Listed by api.describe while set. Message thread.
    using Extension = std::function<juce::var (const juce::String& op, const juce::var& args)>;
    static void setExtension (juce::StringArray operations, Extension handler);

private:
    struct Impl;
    explicit RemoteControl (std::unique_ptr<Impl>);
    std::unique_ptr<Impl> impl;
};
} // namespace lynx
