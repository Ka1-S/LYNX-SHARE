#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

// The channels of a PipeWire node in the node's own port order (round 190rj). PipeWire hands out registry ids as it
// likes (it reuses freed ones), so the ports of a node can come with their ids out of order: a stereo sink whose FR port
// has a lower id than FL played LYNX's left channel on the right, a 16-channel source recorded In 1 = AUX8...
// `port.id` (the port's place on its node) is the order; the registry id only breaks a tie (a port without port.id).
namespace lynx::pwchannels
{
struct Port
{
    int index = -1;          // port.id (-1: not given)
    std::uint32_t id = 0;    // the registry id
    std::string channel;     // audio.channel ("FL", "FR", "AUX0"...)
};

inline std::vector<std::string> inPortOrder (std::vector<Port> ports)
{
    std::stable_sort (ports.begin(), ports.end(), [] (const Port& a, const Port& b) { return std::tie (a.index, a.id) < std::tie (b.index, b.id); });
    std::vector<std::string> out;
    out.reserve (ports.size());
    for (auto& p : ports)
        out.push_back (std::move (p.channel));
    return out;
}
} // namespace lynx::pwchannels
