#pragma once

// --picturetest (round 190bm; a 0.1.25 user saw the Mixer and the Playlist black). Every component under a root that
// paints through a cached picture (juce::CachedComponentImage: the Playlist's lanes, the Mixer's strips) is drawn
// through that picture and, for reference, straight (paintEntireComponent), off screen:
//   - at 100 / 150 / 200 / 300 % (the picture is made at the target's scale),
//   - into a native image (Windows: Direct2D, what a window drawn with the GPU is) and into a software image (what a
//     window drawn without the GPU is: JUCE's software renderer reads the picture's pixels),
//   - both ways round: the picture filled while drawing to one kind of target (by a small repaint, as the playhead's
//     or a meter's, after the view has been still), then shown whole on that kind and on the other. Shown on a
//     software target is 0.1.25's fault: the picture was painted on the GPU, its software copy never written, and
//     a software target showed that copy.
// A case fails when what the picture shows is black where the straight paint is not, or differs from it by more than
// two renderers do. No window is made.

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace lynx::ui::picturecheck
{
struct Stats
{
    double black = 0.0; // share of pixels that are 0, 0, 0
    double mean = 0.0;  // mean of the three channels, 0..255
};

inline Stats statsOf (const juce::Image& image)
{
    Stats s;
    if (image.isNull())
        return s;
    const juce::Image::BitmapData data (image, juce::Image::BitmapData::readOnly);
    juce::int64 black = 0, sum = 0;
    for (int y = 0; y < data.height; ++y)
        for (int x = 0; x < data.width; ++x)
        {
            const auto c = data.getPixelColour (x, y);
            const auto v = (int) c.getRed() + (int) c.getGreen() + (int) c.getBlue();
            black += v == 0 ? 1 : 0;
            sum += v;
        }
    const auto pixels = juce::jmax ((juce::int64) 1, (juce::int64) data.width * data.height);
    s.black = (double) black / (double) pixels;
    s.mean = (double) sum / (3.0 * (double) pixels);
    return s;
}

// mean absolute difference per channel, 0..255
inline double difference (const juce::Image& a, const juce::Image& b)
{
    if (a.isNull() || b.isNull() || a.getBounds() != b.getBounds())
        return 255.0;
    const juce::Image::BitmapData pa (a, juce::Image::BitmapData::readOnly), pb (b, juce::Image::BitmapData::readOnly);
    juce::int64 sum = 0;
    for (int y = 0; y < pa.height; ++y)
        for (int x = 0; x < pa.width; ++x)
        {
            const auto ca = pa.getPixelColour (x, y), cb = pb.getPixelColour (x, y);
            sum += std::abs ((int) ca.getRed() - (int) cb.getRed()) + std::abs ((int) ca.getGreen() - (int) cb.getGreen())
                 + std::abs ((int) ca.getBlue() - (int) cb.getBlue());
        }
    return (double) sum / (3.0 * (double) juce::jmax ((juce::int64) 1, (juce::int64) pa.width * pa.height));
}

// The verdict on one drawing through the picture against the straight paint of the same view (pure: unit-tested)
constexpr double maxBlackExcess = 0.05; // the picture may not be black on over 5 % more of its pixels than the reference
constexpr double maxDifference = 12.0;  // two renderers' anti-aliasing and the playhead / meter overlays stay far below
inline bool pictureOk (const Stats& viaPicture, const Stats& straight, double meanDifference) noexcept
{
    return viaPicture.black <= straight.black + maxBlackExcess && meanDifference <= maxDifference;
}

inline void collect (juce::Component& c, std::vector<juce::Component*>& out)
{
    if (c.getCachedComponentImage() != nullptr)
        out.push_back (&c);
    for (auto* child : c.getChildren())
        collect (*child, out);
}

// The part of `c` its parent shows (what the Mixer's picture holds: the strips are as wide as all the inserts)
inline juce::Rectangle<int> shownPart (juce::Component& c)
{
    auto visible = c.getLocalBounds();
    if (auto* parent = c.getParentComponent())
        visible = visible.getIntersection (c.getLocalArea (parent, parent->getLocalBounds()));
    return visible;
}

// `clip` (not empty): only that part is drawn, as a window does for a small repaint (the playhead, a meter)
inline juce::Image draw (juce::Component& c, juce::Rectangle<int> part, float scale, bool softwareTarget, bool throughPicture, juce::Rectangle<int> clip = {})
{
    const auto w = juce::jmax (1, juce::roundToInt ((float) part.getWidth() * scale)), h = juce::jmax (1, juce::roundToInt ((float) part.getHeight() * scale));
    auto target = softwareTarget ? juce::Image (juce::Image::RGB, w, h, true, juce::SoftwareImageType()) : juce::Image (juce::Image::RGB, w, h, true);
    {
        juce::Graphics g (target);
        g.addTransform (juce::AffineTransform::scale (scale));
        g.setOrigin (-part.getX(), -part.getY());
        if (! clip.isEmpty())
            g.reduceClipRegion (clip);
        if (throughPicture)
            c.getCachedComponentImage()->paint (g);
        else
            c.paintEntireComponent (g, true);
    }
    return target;
}

// Runs every case on the cached components under `root`; `label` names the root and its size in the report
inline juce::String run (juce::Component& root, const juce::String& label, const std::vector<float>& scales, int& failures, const juce::File& dumpFolder)
{
    std::vector<juce::Component*> cached;
    collect (root, cached);
    juce::String out;
    if (cached.empty())
        return label + ": no cached picture (LYNX_PLAYLIST_CACHE=0 / LYNX_MIXER_CACHE=0?)\n";
    auto index = 0;
    for (auto* c : cached)
    {
        const auto part = shownPart (*c);
        const auto name = label + " #" + juce::String (++index) + " (" + juce::String (part.getWidth()) + " x " + juce::String (part.getHeight()) + ")";
        if (part.isEmpty())
        {
            out << name << ": nothing shown\n";
            continue;
        }
        for (const auto scale : scales)
        {
            // the picture filled for a native target, shown again on it, then shown on a software one; then filled
            // for a software target and shown on a native one
            struct Step
            {
                const char* what;
                bool software, refill;
            };
            for (const auto& step : { Step { "filled, native target", false, true }, Step { "shown again, native target", false, false },
                                      Step { "shown, software target", true, false }, Step { "filled, software target", true, true },
                                      Step { "shown, native target", false, false } })
            {
                if (step.refill)
                {
                    // everything invalid, then a small repaint once the view has been still: the Playlist paints
                    // large repaints straight to the window and (re)builds its picture at the next small one (the
                    // playhead's), 300 ms after the last large one. From then on a whole-window paint shows the picture.
                    c->repaint();
                    juce::Thread::sleep (350);
                    draw (*c, part, scale, step.software, true, part.withWidth (juce::jmin (8, part.getWidth())));
                }
                const auto viaPicture = draw (*c, part, scale, step.software, true);
                const auto straight = draw (*c, part, scale, step.software, false);
                const auto a = statsOf (viaPicture), b = statsOf (straight);
                const auto d = difference (viaPicture, straight);
                const auto ok = pictureOk (a, b, d);
                failures += ok ? 0 : 1;
                out << name << " at " << juce::roundToInt (scale * 100.0f) << " %, " << step.what << ": " << (ok ? "ok" : "FAIL") << " (black "
                    << juce::String (a.black * 100.0, 1) << " % vs " << juce::String (b.black * 100.0, 1) << " %, mean " << juce::String (a.mean, 1) << " vs "
                    << juce::String (b.mean, 1) << ", difference " << juce::String (d, 2) << ")\n";
                if (! ok && dumpFolder != juce::File())
                    for (const auto& [suffix, im] : { std::pair<const char*, const juce::Image*> { "picture", &viaPicture }, { "straight", &straight } })
                    {
                        juce::PNGImageFormat png;
                        const auto file = dumpFolder.getChildFile ("picturetest-" + juce::String (failures) + "-" + suffix + ".png");
                        file.deleteFile();
                        juce::FileOutputStream stream (file);
                        if (stream.openedOk())
                            png.writeImageToStream (*im, stream);
                    }
            }
        }
    }
    return out;
}
} // namespace lynx::ui::picturecheck
