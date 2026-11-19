#include "ui/PickerLook.h"

#include "ui/Theme.h"

#if JUCE_WINDOWS
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

namespace lynx::pickerlook
{
// (FL's form is see-through: the desktop and FL show through its veil)
juce::Image captureScreen (juce::Rectangle<int> physical)
{
#if JUCE_WINDOWS
    if (physical.isEmpty())
        return {};
    const auto w = physical.getWidth(), h = physical.getHeight();
    auto* screen = GetDC (nullptr);
    auto* dc = CreateCompatibleDC (screen);
    BITMAPINFO info {};
    info.bmiHeader.biSize = sizeof (BITMAPINFOHEADER);
    info.bmiHeader.biWidth = w;
    info.bmiHeader.biHeight = -h;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    auto* bitmap = CreateDIBSection (dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    juce::Image out;
    if (bitmap != nullptr && bits != nullptr)
    {
        auto* old = SelectObject (dc, bitmap);
        if (BitBlt (dc, 0, 0, w, h, screen, physical.getX(), physical.getY(), SRCCOPY))
        {
            GdiFlush();
            out = juce::Image (juce::Image::RGB, w, h, false, juce::SoftwareImageType());
            juce::Image::BitmapData data (out, juce::Image::BitmapData::writeOnly);
            for (int y = 0; y < h; ++y)
            {
                const auto* row = static_cast<const std::uint8_t*> (bits) + (std::size_t) y * (std::size_t) w * 4;
                auto* dest = data.getLinePointer (y);
                for (int x = 0; x < w; ++x)
                {
                    // (JUCE's RGB images are B, G, R in memory on little-endian machines, as the DIB is)
                    dest[x * data.pixelStride + 0] = row[x * 4 + 0];
                    dest[x * data.pixelStride + 1] = row[x * 4 + 1];
                    dest[x * data.pixelStride + 2] = row[x * 4 + 2];
                }
            }
        }
        SelectObject (dc, old);
        DeleteObject (bitmap);
    }
    DeleteDC (dc);
    ReleaseDC (nullptr, screen);
    return out;
#else
    juce::ignoreUnused (physical);
    return {};
#endif
}

void paintVeil (juce::Graphics& g, const juce::Component& owner, const juce::Image& backdrop, juce::Image& backdropShown, double pixelScale)
{
    // FL: the screen shows through a veil of 236/256, a vertical blend of two dark tones that also darkens towards
    // the top and bottom edges over 96 px (cubic). Composed once per size: a hover repaints only a small area.
    const auto w = juce::jmax (1, juce::roundToInt (owner.getWidth() * pixelScale)), h = juce::jmax (1, juce::roundToInt (owner.getHeight() * pixelScale));
    if (! backdropShown.isValid() || backdropShown.getWidth() != w || backdropShown.getHeight() != h)
    {
        backdropShown = juce::Image (juce::Image::RGB, w, h, false);
        juce::Graphics b (backdropShown);
        b.fillAll (juce::Colours::black);
        if (backdrop.isValid())
            b.drawImage (backdrop, backdropShown.getBounds().toFloat());
        const auto top = juce::Colour (0x23, 0x20, 0x28), bottom = juce::Colour (0x17, 0x22, 0x28); // (FL's two tones)
        const auto edge = 96.0 * pixelScale;
        for (int y = 0; y < h; ++y)
        {
            auto c = top.interpolatedWith (bottom, (float) y / (float) juce::jmax (1, h - 1));
            double k = 1.0;
            if (y < edge)
            {
                const auto f = 1.0 - y / edge;
                k = 1.0 - f * f * f;
            }
            else if (y > h - edge)
            {
                const auto f = (y - (h - edge)) / edge;
                k = 1.0 - f * f * f;
            }
            c = juce::Colour::fromFloatRGBA (c.getFloatRed() * (float) k, c.getFloatGreen() * (float) k, c.getFloatBlue() * (float) k, 236.0f / 256.0f);
            b.setColour (c);
            b.fillRect (0, y, w, 1);
        }
    }
    g.drawImage (backdropShown, owner.getLocalBounds().toFloat());
}


// FL's highlight behind the tile pointed at: the tile ± 3 px and 6 % of its width, the row's height + 14 px above and
// below, rounded; the tab pointed at likewise (± 18 px, no widening)
juce::Rectangle<float> highlight (juce::Rectangle<int> b, int rowHeight, bool isTab)
{
    const auto widen = isTab ? 3.0f : 3.0f + juce::roundToInt (b.getWidth() * 0.06f) + 1.0f;
    const auto half = rowHeight / 2.0f + (isTab ? 18.0f : 14.0f);
    return { (float) b.getX() - widen, (float) b.getCentreY() - half, (float) b.getWidth() + 2.0f * widen, 2.0f * half };
}


void paintHighlight (juce::Graphics& g, juce::Rectangle<float> box, bool pressed)
{
    // FL (measured on its hover): a cool tint, strongest under the top edge, a bright glow at the middle of the top
    // edge and of each side, and a thin rim. Held down, it takes the warm press colour (LYNX: the accent).
    using namespace theme;
    const auto base = pressed ? colour::accent : juce::Colour (0xff6cc4e8);
    const auto light = pressed ? colour::accent.brighter (0.7f) : juce::Colour (0xffb3ffff);
    juce::Graphics::ScopedSaveState keep (g);
    juce::Path shape;
    shape.addRoundedRectangle (box, 9.0f);
    juce::ColourGradient fill (base.withAlpha (pressed ? 0.55f : 0.30f), 0.0f, box.getY(), base.withAlpha (pressed ? 0.30f : 0.13f), 0.0f, box.getBottom(), false);
    fill.addColour (0.55, base.withAlpha (pressed ? 0.26f : 0.10f));
    g.setGradientFill (fill);
    g.fillPath (shape);
    g.reduceClipRegion (shape);
    const auto glow = [&] (float x, float y, float rx, float a) {
        g.setGradientFill (juce::ColourGradient (light.withAlpha (a), x, y, light.withAlpha (0.0f), x + rx, y, true));
        g.fillEllipse (x - rx, y - rx, rx * 2.0f, rx * 2.0f);
    };
    glow (box.getCentreX(), box.getY(), juce::jmin (box.getWidth() * 0.32f, 60.0f), 0.55f);
    glow (box.getX(), box.getCentreY(), 14.0f, 0.35f);
    glow (box.getRight(), box.getCentreY(), 14.0f, 0.35f);
    g.setColour (base.withAlpha (pressed ? 0.9f : 0.6f));
    g.strokePath (shape, juce::PathStrokeType (1.2f));
    juce::ColourGradient line (light.withAlpha (0.0f), box.getX(), 0.0f, light.withAlpha (0.0f), box.getRight(), 0.0f, false);
    line.addColour (0.5, light);
    g.setGradientFill (line);
    g.fillRect (box.getX(), box.getY(), box.getWidth(), 1.5f);
}

} // namespace lynx::pickerlook
