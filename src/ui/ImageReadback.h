#pragma once

// Drawing into a cached picture without JUCE's readback (round 174e2). JUCE 8.0.4 keeps a software copy of every
// Direct2D image and copies the whole image back from the GPU each time a Graphics drawing into it ends. LYNX's cached
// pictures (the Playlist's, the Mixer's) are only ever drawn to the window, never read, so that readback (8 MB for a
// full-screen Playlist, and a GPU stall) is pure cost. cmake/LynxPatchJuce.cmake (fix 15) adds the flag; a Graphics
// made while a ScopedNoImageReadback lives skips the readback. Other platforms: nothing to skip.

#include <juce_graphics/juce_graphics.h>

#if JUCE_WINDOWS
namespace juce
{
extern bool lynxSkipImageReadback; // juce_Direct2DImage_windows.cpp (patched)
}
#endif

namespace lynx::ui
{
struct ScopedNoImageReadback
{
#if JUCE_WINDOWS
    ScopedNoImageReadback() : before (juce::lynxSkipImageReadback) { juce::lynxSkipImageReadback = true; }
    ~ScopedNoImageReadback() { juce::lynxSkipImageReadback = before; }
    bool before;
#else
    ScopedNoImageReadback() = default;
#endif
    ScopedNoImageReadback (const ScopedNoImageReadback&) = delete;
    ScopedNoImageReadback& operator= (const ScopedNoImageReadback&) = delete;
};
} // namespace lynx::ui
