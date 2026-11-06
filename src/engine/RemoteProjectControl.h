#pragma once

#include "core/Song.h"
#include <juce_core/juce_core.h>
#include <deque>
#include <functional>

namespace lynx
{
class AudioEngine;
namespace remote_document
{
constexpr std::size_t maxBytes = 64 * 1024 * 1024;
constexpr int maxPage = 65536;
std::string bytes (const Song&);
std::string hash (const std::string&);
Song stage (const Song&, const juce::var& patch);
}

bool isRemoteProjectOperation (const juce::String&);
// One object belongs to one remote-control server. Snapshots are immutable and bounded.
class RemoteProjectControl final
{
public:
    explicit RemoteProjectControl (AudioEngine& value) : engine (value) {}
    juce::var dispatch (const juce::String& operation, const juce::var& args, const std::function<bool()>& cancelled = {});

private:
    struct Snapshot
    {
        juce::String id;
        std::string project, document, digest;
        std::uint64_t revision;
    };
    AudioEngine& engine;
    std::deque<Snapshot> snapshots;
};
} // namespace lynx
