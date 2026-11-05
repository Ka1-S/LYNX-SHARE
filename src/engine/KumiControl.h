#pragma once

#include <functional>
#include <memory>
#include <juce_core/juce_core.h>

namespace lynx
{
class AudioEngine;

// Opt-in local control for Kumi. Nothing is opened unless LYNX_KUMI_DISCOVERY
// names an absolute discovery file. The owner must outlive this object.
class KumiControl final
{
public:
    static std::unique_ptr<KumiControl> create (AudioEngine&, std::function<juce::String()> projectName,
                                                 std::function<juce::String()> projectPath = {});
    ~KumiControl();

private:
    struct Impl;
    explicit KumiControl (std::unique_ptr<Impl>);
    std::unique_ptr<Impl> impl;
};
} // namespace lynx
