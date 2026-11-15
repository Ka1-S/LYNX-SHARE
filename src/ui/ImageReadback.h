#pragma once

// Drawing into a cached picture without JUCE's readback (round 174e2). JUCE 8.0.4 keeps a software copy of every
// Direct2D image and copies the whole image back from the GPU each time a Graphics drawing into it ends. LYNX's cached
// pictures (the Playlist's, the Mixer's) are normally only drawn to the window, never read, so that readback (8 MB for
// a full-screen Playlist, and a GPU stall) is pure cost. cmake/LynxPatchJuce.cmake (fix 16) adds the flag; a Graphics
// made while a ScopedNoImageReadback lives puts the readback off. Other platforms: nothing to skip.
//
// Round 190bm (0.1.25 showed the Mixer and the Playlist black): "put off", not "never". In 0.1.25 the software copy
// simply stayed as allocated (black), and JUCE reads that copy whenever the picture is drawn by anything but the
// Direct2D device it was painted on: a window drawn without the GPU (Options > General settings > GPU rendering off,
// which LYNX also switches off by itself after a crash in the graphics driver), a window on a monitor of another
// graphics adapter than the system's first, or the pages of a new device after the adapters changed. The patch
// (fix 22) now reads the picture back when the copy is first needed, and counts pictures it could not read
// (pictureLossCount: the caches repaint). A window drawn without the GPU gets a software picture in the first place.

#include "platform/CrashGuard.h"

#include <juce_gui_basics/juce_gui_basics.h>

#if JUCE_WINDOWS
namespace juce
{
class ImagePixelData;
extern bool lynxSkipImageReadback;                // juce_Direct2DImage_windows.cpp (patched)
extern const ImagePixelData* lynxNoReadbackImage; // (patched) the one image the flag is for
extern int lynxImageContentLost;                  // (patched) device pictures lost before they could be read back
}
#endif

namespace lynx::ui
{
// While it lives, a Graphics made on `image` ends without the readback (any other image: as ever)
struct ScopedNoImageReadback
{
#if JUCE_WINDOWS
    explicit ScopedNoImageReadback (const juce::Image& image) : before (juce::lynxSkipImageReadback), imageBefore (juce::lynxNoReadbackImage)
    {
        juce::lynxSkipImageReadback = true;
        juce::lynxNoReadbackImage = image.getPixelData();
    }
    ~ScopedNoImageReadback()
    {
        juce::lynxSkipImageReadback = before;
        juce::lynxNoReadbackImage = imageBefore;
    }
    bool before;
    const juce::ImagePixelData* imageBefore;
#else
    explicit ScopedNoImageReadback (const juce::Image&) {}
#endif
    ScopedNoImageReadback (const ScopedNoImageReadback&) = delete;
    ScopedNoImageReadback& operator= (const ScopedNoImageReadback&) = delete;
};

// Changes when a picture drawn without readback was lost with its device: a cache holding one paints it again
inline int pictureLossCount() noexcept
{
#if JUCE_WINDOWS
    return juce::lynxImageContentLost;
#else
    return 0;
#endif
}

// After a cache drew its frame: true when a picture was lost while it was drawn (its graphics device went away under
// a readback), so the frame may show it black: the cache then asks for one more paint. `retries` (the cache's own
// count) stops a device that keeps failing from repainting for ever.
inline bool pictureLostMeanwhile (int lossesSeen, int& retries) noexcept
{
    if (pictureLossCount() == lossesSeen)
    {
        retries = 0;
        return false;
    }
    return ++retries <= 3;
}

// The window of `c` is drawn by the CPU (Windows: JUCE's "Software Renderer", engine 0): its cached pictures are
// software images, drawn and shown without the GPU. No window (off-screen tests) and other platforms: false.
inline bool windowDrawnByCpu (const juce::Component& c)
{
#if JUCE_WINDOWS
    if (auto* peer = c.getPeer())
        return peer->getAvailableRenderingEngines().size() > 1 && peer->getCurrentRenderingEngine() == 0;
#endif
    juce::ignoreUnused (c);
    return false;
}

// A cached picture's image, cleared. Null when it cannot be had (no size, or more pixels than any window LYNX draws:
// the caller then paints its component straight to the window). `software`: see windowDrawnByCpu.
constexpr juce::int64 maxPicturePixels = 16384 * 8192; // (an 8K display at 200 % is 7680 x 4320)
inline juce::Image makePicture (int width, int height, bool software)
{
    if (width <= 0 || height <= 0 || (juce::int64) width * height > maxPicturePixels)
        return {};
    try
    {
        return software ? juce::Image (juce::Image::RGB, width, height, true, juce::SoftwareImageType())
                        : juce::Image (juce::Image::RGB, width, height, true);
    }
    catch (const std::bad_alloc&)
    {
        return {};
    }
}

// A picture that is kept and drawn to the window again and again, as the window's own kind of image (round 190gv).
// JUCE 8.0.4's Direct2D drawImage turns a software image into a NEW GPU bitmap (and uploads it) on every draw; a
// Direct2D image keeps its GPU copy, so it is made once. The same pixels (the software picture is copied as it is).
// Only on the message thread (the Direct2D images are not made elsewhere: video export, the OpenGL renderer's
// thread); elsewhere, and on other platforms, the picture itself.
inline juce::Image forScreen (const juce::Image& picture)
{
#if JUCE_WINDOWS
    if (picture.isValid() && juce::MessageManager::existsAndIsCurrentThread()
        && picture.getPixelData()->createType()->getTypeID() == juce::SoftwareImageType().getTypeID())
        return juce::NativeImageType().convert (picture);
#endif
    return picture;
}

// A session step for the reports (round 190bm): what a cache's picture is and what draws the window, once per kind of
// picture and scale ("Playlist picture: 2880 x 1500 px at 150 %, Direct2D image, window drawn with the GPU"). `last`
// is the cache's own memory of what it noted.
inline void notePicture (const char* name, const juce::Image& picture, float scale, bool software, const juce::Component& owner, juce::String& last)
{
    const auto drawn = owner.getPeer() == nullptr ? "no window" : windowDrawnByCpu (owner) ? "window drawn without the GPU" : "window drawn with the GPU";
   #if JUCE_WINDOWS
    const char* const nativeKind = "Direct2D image";
   #else
    const char* const nativeKind = "native image";
   #endif
    const auto kind = picture.isNull() ? juce::String ("no picture (painted straight to the window)")
                                       : juce::String (software ? "software image" : nativeKind);
    const auto key = kind + ", " + drawn + " at " + juce::String (juce::roundToInt (scale * 100.0f)) + " %";
    if (key == last)
        return;
    last = key;
    crashguard::note ((juce::String (name) + " picture: " + juce::String (picture.getWidth()) + " x " + juce::String (picture.getHeight()) + " px at "
                       + juce::String (juce::roundToInt (scale * 100.0f)) + " %, " + kind + ", " + drawn)
                          .toStdString());
}
} // namespace lynx::ui
