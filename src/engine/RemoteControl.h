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

private:
    struct Impl;
    explicit RemoteControl (std::unique_ptr<Impl>);
    std::unique_ptr<Impl> impl;
};
} // namespace lynx
