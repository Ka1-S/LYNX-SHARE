// The macOS part of ui/MotionOverlay (round 190jx): see MotionOverlayMac.h. Manual retain / release (-fno-objc-arc).

#include "ui/MotionOverlayMac.h"

#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace lynx::ui::maclayer
{
namespace
{
// a layer that never animates what it is given (position, bounds, contents, the clip's frame)
void noActions (CALayer* layer)
{
    NSNull* none = [NSNull null];
    layer.actions = @{ @"position": none, @"bounds": none, @"frame": none, @"contents": none, @"hidden": none, @"sublayers": none,
                       @"onOrderIn": none, @"onOrderOut": none, @"transform": none, @"anchorPoint": none, @"contentsScale": none };
}

CGImageRef makeImage (const std::uint8_t* pixels, int w, int h, int stride)
{
    const auto bytes = (std::size_t) stride * (std::size_t) h;
    CFDataRef data = CFDataCreate (nullptr, pixels, (CFIndex) bytes);
    if (data == nullptr)
        return nullptr;
    CGDataProviderRef provider = CGDataProviderCreateWithCFData (data);
    CFRelease (data);
    CGColorSpaceRef space = CGColorSpaceCreateWithName (kCGColorSpaceSRGB);
    // JUCE's ARGB image: premultiplied, 0xAARRGGBB in little-endian words (BGRA bytes)
    CGImageRef image = CGImageCreate ((size_t) w, (size_t) h, 8, 32, (size_t) stride, space,
                                      (CGBitmapInfo) kCGBitmapByteOrder32Little | (CGBitmapInfo) kCGImageAlphaPremultipliedFirst, provider, nullptr,
                                      false, kCGRenderingIntentDefault);
    CGColorSpaceRelease (space);
    CGDataProviderRelease (provider);
    return image;
}
} // namespace

bool enabled()
{
    static const bool on = [] {
        const auto* v = std::getenv ("LYNX_OVERLAY_MAC");
        return v != nullptr && std::strcmp (v, "1") == 0;
    }();
    return on;
}

double backingScale (void* nsView)
{
    auto* view = (NSView*) nsView;
    if (view == nil || view.window == nil)
        return 1.0;
    return (double) view.window.backingScaleFactor;
}

bool subviewOver (void* nsView, Rect r)
{
    auto* view = (NSView*) nsView;
    if (view == nil)
        return false;
    const auto scale = backingScale (nsView);
    const auto area = NSMakeRect ((double) r.x / scale, (double) r.y / scale, (double) r.w / scale, (double) r.h / scale);
    for (NSView* sub in view.subviews)
    {
        if (sub.hidden)
            continue;
        auto f = sub.frame; // (in the view's coordinates: top-left origin, JUCE's view is flipped)
        if (! view.flipped)
            f.origin.y = view.bounds.size.height - f.origin.y - f.size.height;
        if (NSIntersectsRect (f, area))
            return true;
    }
    return false;
}

struct Sprite::State
{
    CALayer* clip = nil; // (masks the mark to the view's visible part)
    CALayer* mark = nil;
    NSView* view = nil;  // (not retained: JUCE's)
    std::vector<CGImageRef> images = std::vector<CGImageRef> (64, nullptr);
    int slot = -1;

    ~State()
    {
        for (auto* i : images)
            if (i != nullptr)
                CGImageRelease (i);
        [mark release];
        [clip release];
    }

    void make()
    {
        if (clip != nil)
            return;
        clip = [[CALayer alloc] init];
        mark = [[CALayer alloc] init];
        noActions (clip);
        noActions (mark);
        clip.masksToBounds = YES;
        clip.zPosition = 1000.0; // (above the view's own content; sibling views' layers are separate views)
        mark.anchorPoint = CGPointMake (0.0, 0.0);
        clip.anchorPoint = CGPointMake (0.0, 0.0);
        mark.magnificationFilter = kCAFilterLinear;
        mark.minificationFilter = kCAFilterLinear;
        [clip addSublayer: mark];
    }
};

Sprite::Sprite() : s (new State()) {}

Sprite::~Sprite()
{
    restore();
    delete s;
}

bool Sprite::upload (int slot, const std::uint8_t* pixels, int w, int h, int stride)
{
    if (slot < 0 || slot >= (int) s->images.size())
        return false;
    auto* image = makeImage (pixels, w, h, stride);
    if (image == nullptr)
        return false;
    if (s->images[(std::size_t) slot] != nullptr)
        CGImageRelease (s->images[(std::size_t) slot]);
    s->images[(std::size_t) slot] = image;
    return true;
}

bool Sprite::place (void* nsView, int slot, int x, int y, double shift, Rect clipRect)
{
    auto* view = (NSView*) nsView;
    if (view == nil || view.layer == nil || slot < 0 || slot >= (int) s->images.size() || s->images[(std::size_t) slot] == nullptr)
        return false;
    s->make();
    const auto scale = backingScale (nsView);
    auto* image = s->images[(std::size_t) slot];
    const auto w = (double) CGImageGetWidth (image), h = (double) CGImageGetHeight (image);
    CALayer* host = view.layer;
    const auto hostH = (double) host.bounds.size.height;
    // the layer's coordinates: points, y down when the view's layer is flipped (AppKit flips a flipped view's layer), else up
    const auto flipped = (bool) host.geometryFlipped;
    auto yOf = [&] (double topPx, double heightPx) { return flipped ? topPx / scale : hostH - (topPx + heightPx) / scale; };

    [CATransaction begin];
    [CATransaction setDisableActions: YES];
    if (s->view != view || s->clip.superlayer != host)
    {
        [s->clip removeFromSuperlayer];
        [host addSublayer: s->clip];
        s->view = view;
    }
    s->clip.geometryFlipped = flipped;
    s->clip.frame = CGRectMake ((double) clipRect.x / scale, yOf ((double) clipRect.y, (double) clipRect.h), (double) clipRect.w / scale,
                                (double) clipRect.h / scale);
    s->clip.hidden = clipRect.empty();
    if (s->slot != slot)
    {
        s->mark.contents = (id) image;
        s->slot = slot;
    }
    s->mark.contentsScale = scale;
    // inside the clip (same flip): top-left at (x + shift, y) device pixels of the view
    const auto mx = ((double) x + shift - (double) clipRect.x) / scale;
    const auto my = flipped ? ((double) y - (double) clipRect.y) / scale : ((double) clipRect.y + (double) clipRect.h - ((double) y + h)) / scale;
    s->mark.frame = CGRectMake (mx, my, w / scale, h / scale);
    [CATransaction commit];
    isDrawn = true;
    return true;
}

void Sprite::restore()
{
    if (s->clip != nil && s->clip.superlayer != nil)
    {
        [CATransaction begin];
        [CATransaction setDisableActions: YES];
        [s->clip removeFromSuperlayer];
        [CATransaction commit];
    }
    s->view = nil;
    isDrawn = false;
}

void Sprite::forget()
{
    restore();
}

const char* selfTest (void* nsView)
{
    auto* view = (NSView*) nsView;
    if (view == nil)
        return "no view";
    if (view.layer == nil)
        return "the view has no layer (not layer-backed)";
    const int W = 200, H = 100;
    // a 6 x 40 picture: a 1 px opaque red line at x = 2
    std::vector<std::uint32_t> pixels (6 * 40, 0u);
    for (int y = 0; y < 40; ++y)
        pixels[(std::size_t) (y * 6 + 2)] = 0xffff0000u;
    const char* result = "";
    {
        Sprite sprite;
        const auto scale = backingScale (nsView);
        const auto px = [scale] (double points) { return (int) std::lround (points * scale); };
        if (! sprite.upload (0, reinterpret_cast<const std::uint8_t*> (pixels.data()), 6, 40, 6 * 4))
            return "picture not made";
        // the line at x = 50 points (device: 50 * scale + 2), clipped to x 0..60 points, y 0..30 points
        if (! sprite.place (nsView, 0, px (50.0) - 2, 0, 0.0, { 0, 0, px (60.0), px (30.0) }))
            return "not placed";
        if (sprite.s->clip.superlayer != view.layer || sprite.s->mark.contents == nil)
            return "the layer is not on the view's layer";
        // what the layers show: the view's layer rendered (its own content, a Metal layer, is not drawn by renderInContext;
        // the mark's layer is)
        CGColorSpaceRef space = CGColorSpaceCreateWithName (kCGColorSpaceSRGB);
        std::vector<std::uint32_t> out ((std::size_t) (W * H), 0u);
        CGContextRef ctx = CGBitmapContextCreate (out.data(), (size_t) W, (size_t) H, 8, (size_t) W * 4, space,
                                                  (CGBitmapInfo) kCGBitmapByteOrder32Little | (CGBitmapInfo) kCGImageAlphaPremultipliedFirst);
        CGColorSpaceRelease (space);
        if (ctx == nullptr)
            return "no bitmap context";
        [sprite.s->clip renderInContext: ctx];
        CGContextRelease (ctx);
        // within the clip layer's own rendering (points, 1:1 here): a red column at x = 50, and nothing red beyond 60
        int redAt = -1, redBeyond = 0;
        for (int yy = 0; yy < H; ++yy)
            for (int xx = 0; xx < W; ++xx)
            {
                const auto v = out[(std::size_t) (yy * W + xx)];
                const auto r = (int) ((v >> 16) & 0xff), a = (int) (v >> 24);
                if (a > 0x60 && r > 0x60)
                {
                    if (redAt < 0)
                        redAt = xx;
                    if (xx >= 60)
                        ++redBeyond;
                }
            }
        if (redAt < 49 || redAt > 51)
            result = "the mark is not where it was put";
        else if (redBeyond > 0)
            result = "the mark is drawn outside its clip";
        sprite.restore();
        if (sprite.s->clip.superlayer != nil)
            result = "the layer stayed after restore()";
    }
    return result;
}
} // namespace lynx::ui::maclayer
