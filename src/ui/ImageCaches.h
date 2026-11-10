#pragma once

#include <functional>
#include <utility>
#include <vector>

namespace lynx::ui
{
// Static caches of juce::Image register how to empty themselves (message thread only). On Windows a JUCE image is
// backed by Direct2D, and the last image holds JUCE's DirectX device. Inside a plugin (round 180b) the statics would
// only be freed when the host unloads the DLL, under the loader lock, where releasing the device waits for the graphics
// driver's threads forever: FL Studio hung on exit. A plugin empties every cache when its last instance closes.
inline std::vector<std::function<void()>>& imageCacheClears()
{
    static std::vector<std::function<void()>> clears;
    return clears;
}

// returns true, so a cache can register from its own static initialiser
inline bool registerImageCache (std::function<void()> clear)
{
    imageCacheClears().push_back (std::move (clear));
    return true;
}

inline void clearImageCaches()
{
    for (auto& clear : imageCacheClears())
        clear();
}
} // namespace lynx::ui
