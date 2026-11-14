#include "core/FlCurve.h"
#include "core/DrawMode.h"
#include "core/LayoutStyle.h"
#include "ui/TempoDetectionDialog.h"
#include "ui/ClipGridComponent.h"
#include "ui/CleanVoiceMenu.h"
#include "ui/FlMods.h"
#include "ui/Motion.h"
#include "core/FlSettings.h"
#include "core/RulerGestures.h"
#include "ui/ColourPicker.h"
#include "ui/TrackIcons.h"
#include "core/TrackIcons.h"
#include "core/WarpEdit.h"
#include "ui/ClipProperties.h"
#include "ui/UiTrace.h"
#include "ui/ImageReadback.h"
#include "ui/TextCache.h"
#include "ui/PlaylistComponent.h"
#include "core/ChainPresets.h"

#include "core/EventEdit.h"
#include "ui/EventEditor.h"
#include "ui/PatternBlocksView.h"
#include "ui/VideoTracks.h"
#include "core/PatternBlocks.h"
#include "ui/StemExtract.h"
#include "ui/AudioToMidi.h"

#include "core/KeyMap.h"
#include "core/MarkerPlan.h"
#include "core/Performance.h"
#include "core/Templates.h"

#include "core/TransportEdit.h"

#include "core/ClipOps.h"
#include "core/FlClipMenu.h"
#include "core/Rack.h"
#include "core/Slicer.h"

#include "core/EditorPolish.h"

#include "core/RackOps.h"

#include "core/NoteEdit.h"
#include "core/PlaylistOps.h"
#include "core/PlaylistTrackOps.h"
#include "core/KeyFollow.h"
#include "core/SimpleTracks.h"
#include "core/QuickRender.h"
#include "core/TempoDetect.h"
#include "core/Comping.h"
#include "core/ExportOptions.h"
#include "core/TakeRecording.h"
#include "ui/ExportDialog.h"
#include "ui/MixerComponent.h"
#include "engine/Text.h"
#include "ui/BrowserComponent.h"
#include "ui/ClipGridComponent.h"
#include "core/SessionOps.h"
#include "ui/AutomationUi.h"
#include "ui/ChannelSettings.h"
#include "ui/Dialogs.h"
#include "ui/HintBar.h"
#include "ui/I18n.h"
#include "ui/Theme.h"
#include "ui/WaveformView.h"
#include "ui/AudioEditor.h"
#include "ui/PluginPicker.h"
#include "ui/QuickPick.h"

#include <map>
#include <unordered_map>

namespace lynx
{
namespace
{
constexpr float clipTitleHeight = 13.0f;

// FL's "+" at the top left of the track headers (TEVENTEDITFORM TLEventPanel.AddInstrBtn, 21 x 18, hint "Add instrument
// track / Add audio track"; round 190dx): a left click opens its menu (AddInstrBtnBeforePopup), a right click adds an
// audio track at once (AddInstrBtnMouseUp, right button -> FUN_012e3640); a middle click opens the "Select generator
// plugin" dialog, as the menu's More plugins... (live check 190ew PL-4)
class AddTrackButton : public juce::Component, public juce::SettableTooltipClient
{
public:
    std::function<void()> onMenu, onAddAudioTrack, onMorePlugins;
    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        const auto over = isMouseOverOrDragging();
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        fillRoundRect (g, r, radius::sm, over ? colour::bg4 : colour::bg3);
        g.setColour (over ? colour::accent : colour::textSecondary);
        const auto c = r.getCentre();
        g.fillRect (juce::Rectangle<float> (c.x - 4.5f, c.y - 0.75f, 9.0f, 1.5f));
        g.fillRect (juce::Rectangle<float> (c.x - 0.75f, c.y - 4.5f, 1.5f, 9.0f));
    }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! getLocalBounds().contains (e.getPosition()))
            return;
        if (e.mods.isMiddleButtonDown())
        {
            if (onMorePlugins)
                onMorePlugins();
        }
        else if (flmods::popup (e.mods))
        {
            if (onAddAudioTrack)
                onAddAudioTrack();
        }
        else if (e.mods.isLeftButtonDown() && onMenu)
            onMenu();
    }
};

// --- clip look -------------------------------------------------------------------------------------
// One recipe for every clip kind (studied from FL's Playlist, drawn with LYNX's own code and palette): a muted
// header bar holding a small kind glyph and the name, a body in a deep tint of the clip colour, and the
// content (waveform, notes, curve) in a light tint of it; a thin dark edge separates neighbours. Few elements,
// one hue per clip, so a busy Playlist stays calm and every clip reads at a glance.

enum class ClipGlyph { pattern, audio, automation };

// Text colour readable on a clip's header bar.
juce::Colour clipTitleText (juce::Colour band)
{
    return band.getPerceivedBrightness() > 0.62f ? theme::colour::bg0 : juce::Colours::white.withAlpha (0.92f);
}

// the header keeps a hint of the clip colour, greyed so the names read and the bodies carry the colour
juce::Colour clipHeaderColour (juce::Colour base) { return base.interpolatedWith (juce::Colour (0xff4b525a), 0.5f); }
juce::Colour clipBodyColour (juce::Colour base) { return base.interpolatedWith (theme::colour::bg1, 0.72f); }
// The content in a light tint of the clip colour; on the pale body of a light theme (issue #95: background #FFFFFF, the
// white-tinted waveform vanished) a dark shade of that body instead.
juce::Colour clipContentColour (juce::Colour base)
{
    const auto body = clipBodyColour (base);
    if (body.getPerceivedBrightness() <= 0.6f)
        return base.interpolatedWith (juce::Colours::white, 0.7f);
    // a dark shade of the body, or of the clip colour when that is darker (a dark clip on white: PR #100's point)
    const auto shade = body.interpolatedWith (juce::Colours::black, 0.65f), deep = base.interpolatedWith (juce::Colours::black, 0.55f);
    return deep.getPerceivedBrightness() < shade.getPerceivedBrightness() ? deep : shade;
}

float clipHeaderHeight (juce::Rectangle<float> r) { return juce::jmin (clipTitleHeight, juce::jmax (0.0f, r.getHeight() - 8.0f)); }

// an audio clip's gain as FL's gain handle shows it (round 166e): "-INF" at the bottom of the range
juce::String gainText (float gain)
{
    const auto db = clipops::clipGainToDb (gain);
    return db <= clipops::clipGainMinDb + 1.0e-6 ? juce::String ("-INF dB") : juce::String (db, 1) + " dB";
}

// The kind glyph at the header's left (the clip menu opens from it): LYNX's own small shapes.
void paintClipGlyph (juce::Graphics& g, juce::Rectangle<float> box, ClipGlyph glyph, juce::Colour colour)
{
    g.setColour (colour);
    const auto c = box.getCentre();
    switch (glyph)
    {
        case ClipGlyph::pattern: // three note bars, stepped
            for (int i = 0; i < 3; ++i)
                g.fillRect (juce::Rectangle<float> (c.x - 4.0f + (float) i * 2.0f, c.y - 3.5f + (float) i * 3.0f, 5.0f, 1.5f));
            break;
        case ClipGlyph::audio: // a small mirrored waveform
        {
            const float heights[] = { 2.0f, 6.0f, 9.0f, 5.0f, 3.0f };
            for (int i = 0; i < 5; ++i)
                g.fillRect (juce::Rectangle<float> (c.x - 4.5f + (float) i * 2.0f, c.y - heights[i] * 0.5f, 1.2f, heights[i]));
            break;
        }
        case ClipGlyph::automation: // a rising curve
        {
            juce::Path p;
            p.startNewSubPath (c.x - 4.5f, c.y + 3.5f);
            p.quadraticTo (c.x + 1.0f, c.y + 3.5f, c.x + 4.5f, c.y - 3.5f);
            g.strokePath (p, juce::PathStrokeType (1.3f));
            break;
        }
    }
}

// The header's glyph place. A muted clip shows its X there, as FL Studio does, however narrow the clip (round 177c).
juce::Rectangle<float> clipGlyphBox (juce::Rectangle<float> title)
{
    return title.withTrimmedLeft (3.0f).withTrimmedRight (2.0f).withWidth (juce::jmin (13.0f, title.getWidth() - 5.0f));
}

constexpr float mutedMarkMinWidth = 14.0f;

// Clip body with the header bar, glyph and name; returns the body below the header. A muted clip leaves the glyph's place
// empty for the X its veil draws on top (paintMutedVeil).
juce::Rectangle<float> paintClipFrame (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour base, const juce::String& name,
                                       ClipGlyph glyph = ClipGlyph::pattern, bool muted = false)
{
    using namespace theme;
    const uitrace::Part tally (uitrace::part::frame);
    const auto title = r.withHeight (clipHeaderHeight (r));

    // zoomed far out (a clip a few pixels wide, often a thousand of them on screen): plain rectangles, the same
    // colours. Rounded shapes and gradients cost a path each on the GPU renderer (round 151: 1 350 clips of
    // Thief's chopped vocals took 20-40 ms a frame), and at this size they look the same.
    if (r.getWidth() < 8.0f)
    {
        g.setColour (clipBodyColour (base));
        g.fillRect (r);
        if (title.getHeight() >= 4.0f)
        {
            g.setColour (clipHeaderColour (base));
            g.fillRect (title);
        }
        return r.withTrimmedTop (title.getHeight());
    }

    g.setColour (clipBodyColour (base));
    g.fillRoundedRectangle (r, radius::sm);

    if (title.getHeight() >= 4.0f)
    {
        // the header band: rounded top corners, square bottom; a lighter top half gives it the slight sheen of a
        // gradient (round 151: drawn with the renderer's rounded-rectangle and rectangle primitives, not a path and
        // a gradient brush, which cost a GPU geometry each for every clip)
        const auto header = clipHeaderColour (base);
        g.setColour (header);
        g.fillRoundedRectangle (title, radius::sm);
        g.fillRect (title.withTrimmedTop (juce::jmin (radius::sm, title.getHeight() * 0.5f)));
        g.setColour (header.brighter (0.06f));
        g.fillRect (title.reduced (juce::jmin (radius::sm, title.getWidth() * 0.5f), 0.0f).withHeight (title.getHeight() * 0.45f).withTrimmedTop (1.0f));
        g.setColour (theme::colour::bg0.withAlpha (0.35f)); // the header's lower edge
        g.fillRect (title.getX(), title.getBottom() - 1.0f, title.getWidth(), 1.0f);

        if (r.getWidth() > 18.0f && title.getHeight() >= 9.0f)
        {
            auto text = title.withTrimmedLeft (3.0f).withTrimmedRight (2.0f);
            if (muted)
                text.removeFromLeft (13.0f);
            else if (r.getWidth() >= 40.0f) // room for the glyph (where the clip menu opens)
                paintClipGlyph (g, text.removeFromLeft (13.0f), glyph, clipTitleText (header).withMultipliedAlpha (0.8f));
            const uitrace::Part tallyTitle (uitrace::part::title);
            g.setColour (clipTitleText (header));
            g.setFont (font (type::caption, true));
            textcache::drawText (g, name, text.withTrimmedLeft (2.0f).toNearestInt(), juce::Justification::centredLeft, true);
        }
    }

    return r.withTrimmedTop (title.getHeight());
}

// A muted clip, as in FL Studio: the whole clip dims, its waveform or notes still readable, and an X takes the glyph's place.
// `lit`: the parts of a take lane's clip its comp plays (round 177c): those stay undimmed, as in Ableton Live's lanes.
void paintMutedVeil (juce::Graphics& g, juce::Rectangle<float> r, const std::vector<juce::Rectangle<float>>& lit = {})
{
    using namespace theme;
    {
        const juce::Graphics::ScopedSaveState keep (g);
        for (const auto& part : lit)
            g.excludeClipRegion (part.getSmallestIntegerContainer());
        g.setColour (colour::bg1.withAlpha (0.55f));
        g.fillRoundedRectangle (r, radius::sm);
    }
    for (const auto& part : lit)
    {
        g.setColour (colour::accent);
        g.fillRect (part.withTop (part.getBottom() - 2.0f));
    }

    const auto title = r.withHeight (clipHeaderHeight (r));
    if (r.getWidth() < mutedMarkMinWidth || title.getHeight() < 9.0f)
        return;

    const auto c = clipGlyphBox (title).getCentre();
    const auto arm = juce::jmin (3.5f, title.getHeight() * 0.5f - 1.0f);
    g.setColour (colour::textSecondary); // a little above the dimmed name, as FL's X is, not the brightest thing on the clip
    g.drawLine (c.x - arm, c.y - arm, c.x + arm, c.y + arm, 1.4f);
    g.drawLine (c.x - arm, c.y + arm, c.x + arm, c.y - arm, 1.4f);
}

void paintClipOutline (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour base, bool selected, bool current)
{
    using namespace theme;
    const uitrace::Part tally (uitrace::part::outline);
    if (r.getWidth() < 8.0f && ! selected) // tiny clip (see paintClipFrame): a dark right edge keeps neighbours apart
    {
        g.setColour (current ? colour::textPrimary.withAlpha (0.55f) : colour::bg0.withAlpha (0.85f));
        g.fillRect (r.getRight() - 1.0f, r.getY(), 1.0f, r.getHeight());
        return;
    }
    if (selected)
    {
        g.setColour (colour::selection);
        g.drawRoundedRectangle (r.reduced (0.5f), radius::sm, 2.0f);
    }
    else
    {
        g.setColour (current ? colour::textPrimary.withAlpha (0.55f) : colour::bg0.withAlpha (0.85f));
        g.drawRoundedRectangle (r.reduced (0.5f), radius::sm, current ? 1.2f : 1.0f);
    }
}

} // namespace

// Zoomed far out, a song of chopped audio shows a thousand clips a few pixels wide. Painted one by one, each is a
// handful of rounded paths (frame, header, waveform, outline), and the GPU renderer pays per path: Thief's Playlist
// took 33-47 ms a frame to scroll (round 151). These clips are gathered here, rectangles per colour and waveforms per
// colour as one path, and filled together after the other clips: a few calls a frame. At this size they look the
// same as the full recipe.
struct PlaylistComponent::SmallClips
{
    static constexpr float maxWidth = 12.0f;
    std::map<juce::uint32, juce::RectangleList<float>> bodies, contents, edges;
    std::map<juce::uint32, juce::Path> waves;

    void fill (juce::Graphics& g)
    {
        // (rectangle by rectangle: the GPU renderer turns a rectangle list into a geometry, which costs more)
        for (auto* layer : { &bodies, &contents })
            for (auto& [argb, list] : *layer)
            {
                g.setColour (juce::Colour (argb));
                for (const auto& r : list)
                    g.fillRect (r);
            }
        const uitrace::Part tally (uitrace::part::batchWaves);
        for (auto& [argb, path] : waves)
        {
            g.setColour (juce::Colour (argb));
            g.fillPath (path);
        }
        for (auto& [argb, list] : edges)
        {
            g.setColour (juce::Colour (argb));
            for (const auto& r : list)
                g.fillRect (r);
        }
    }
};

// The Playlist's image cache (round 174e): JUCE's buffered-to-image cache, plus the playhead drawn over the image and
// the playhead's own repaints leaving the image valid
class PlaylistComponent::PlayheadCache final : public juce::CachedComponentImage
{
public:
    explicit PlayheadCache (PlaylistComponent& o) : owner (o) {}

    void paint (juce::Graphics& g) override
    {
        const auto scale = g.getInternalContext().getPhysicalPixelScaleFactor();
        const auto bounds = owner.getLocalBounds();
        const auto imageBounds = bounds * scale;
        const auto nowMs = juce::Time::getMillisecondCounterHiRes();
        const auto clip = g.getClipBounds();
        const auto lanes = owner.gridArea();

        // (round 190bm) a window drawn without the GPU gets a software picture; a picture lost with its graphics
        // device is painted again; no picture at all (no size, no memory): straight to the window
        const auto software = ui::windowDrawnByCpu (owner);
        const auto losses = ui::pictureLossCount();
        if (image.isNull() || image.getBounds() != imageBounds || software != softwareImage || losses != lossesSeen)
        {
            if (image.isNull() || image.getBounds() != imageBounds || software != softwareImage)
                image = ui::makePicture (imageBounds.getWidth(), imageBounds.getHeight(), software);
            softwareImage = software;
            lossesSeen = losses;
            spare = {};
            valid.clear();
        }
        ui::notePicture ("Playlist", image, scale, softwareImage, owner, noted);
        if (image.isNull())
        {
            drawnView = owner.cacheView();
            owner.paintEntireComponent (g, false);
            return;
        }

        // a zoom gesture left scaled lanes in the picture (round 174f): once it is over they are painted exactly
        if (lanesScaled && ! owner.zoomGesture)
            valid.subtract (lanes);

        // the view moved since the picture was drawn. A scroll by whole pixels (round 174e2): the picture is shifted
        // and only the columns / rows it uncovered are painted (FL scrolls at ~120 fps). A step of a zoom gesture
        // (Ctrl+wheel, round 174f): the lanes are scaled to the new zoom around the same ticks and only the columns a
        // zoom out uncovers are painted (a whole repaint of the lanes costs 15-30 ms on a large project); exact once
        // the gesture is over. Anything else (a track resize, both directions at once, the corner tags): everything
        // is painted again.
        const auto view = owner.cacheView();
        CacheShift shift;
        Zoom zoom;
        if (view != drawnView)
        {
            shift = owner.cacheShift (drawnView, view, scale);
            if (shift.area.isEmpty())
                zoom = zoomFrom (drawnView, view, lanes);
            if (shift.area.isEmpty() && ! zoom.on)
                valid.clear();
        }
        auto validNow = valid;
        if (! shift.area.isEmpty())
        {
            juce::RectangleList<int> moved;
            for (const auto& r : valid)
                if (const auto in = r.getIntersection (shift.area); ! in.isEmpty())
                    moved.addWithoutMerging (in.translated (shift.dx, shift.dy).getIntersection (shift.area));
            validNow.subtract (shift.area);
            validNow.subtract (shift.alsoChanged);
            validNow.add (moved);
        }
        if (zoom.on)
        {
            const auto whole = valid.containsRectangle (lanes);
            validNow.subtract (lanes);
            validNow.subtract (owner.rulerArea()); // (the ruler follows the zoom: painted exactly)
            if (whole)
                validNow.add (zoom.covered);
        }

        // a repaint of (almost) everything (an edit): painted straight to the window (through the picture it costs a
        // render and a full blit); the picture is rebuilt once the view has been still for a moment, by the next
        // playhead frame. Not a scroll or a zoom: they go through the picture, so the next frame can shift or scale it
        // (painted straight, every scroll frame found the picture gone and painted everything again)
        auto invalid = juce::RectangleList<int> (bounds);
        invalid.subtract (validNow);
        const auto half = 0.5 * bounds.getWidth() * bounds.getHeight();
        if ((double) clip.getWidth() * clip.getHeight() > half && area (invalid) > half)
            lastLargeMs = nowMs;
        if (nowMs - lastLargeMs < 300.0 && shift.area.isEmpty() && ! zoom.on && ! validNow.containsRectangle (clip) && ! owner.zoomRecent (nowMs))
        {
            if (view != drawnView)
            {
                valid.clear(); // (the picture is of another view now; later invalidations are in this one's coordinates)
                drawnView = view;
            }
            owner.paintEntireComponent (g, false);
            return;
        }

        const auto moving = ! shift.area.isEmpty() || zoom.on;
        if (moving || ! invalid.isEmpty())
        {
            if (moving && (spare.isNull() || spare.getBounds() != image.getBounds()))
            {
                spare = ui::makePicture (image.getWidth(), image.getHeight(), softwareImage);
                if (spare.isNull()) // (no memory for the second picture: this frame straight to the window)
                {
                    valid.clear();
                    drawnView = view;
                    owner.paintEntireComponent (g, false);
                    return;
                }
            }
            auto& target = moving ? spare : image;
            const ui::ScopedNoImageReadback noReadback (target); // (the picture is normally only drawn to the window)
            juce::Graphics imageGraphics (target);
            if (moving)
            {
                // the whole picture, then the moving area over it, shifted or scaled: plain rectangular clips (the
                // picture with a hole cut out is a clip region the GPU renderer masks with a layer; round 174f)
                imageGraphics.drawImageAt (image, 0, 0);
                const juce::Graphics::ScopedSaveState state (imageGraphics);
                if (! shift.area.isEmpty())
                {
                    if (imageGraphics.reduceClipRegion ((shift.area.toFloat() * scale).getSmallestIntegerContainer()))
                        imageGraphics.drawImageAt (image, juce::roundToInt ((float) shift.dx * scale), juce::roundToInt ((float) shift.dy * scale));
                }
                else if (! zoom.covered.isEmpty() && imageGraphics.reduceClipRegion ((zoom.covered.toFloat() * scale).getSmallestIntegerContainer()))
                {
                    imageGraphics.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
                    imageGraphics.drawImageTransformed (image, juce::AffineTransform ((float) zoom.a, 0.0f, (float) zoom.b * scale, 0.0f, 1.0f, 0.0f), false);
                }
            }
            if (! invalid.isEmpty())
            {
                imageGraphics.addTransform (juce::AffineTransform::scale (scale));
                paintExactly (imageGraphics, invalid, validNow); // (the picture without the playhead: drawn over it below)
            }
        }
        if (moving)
            std::swap (image, spare);
        if (zoom.on)
        {
            lanesScaled = true;
            ++scaledSteps;
        }
        else if (! moving && ! validNow.intersectsRectangle (lanes))
        {
            lanesScaled = false; // (the lanes were all painted exactly)
            scaledSteps = 0;
        }
        valid = bounds;
        drawnView = view;

        g.setColour (juce::Colours::black);
        g.drawImageTransformed (image,
                                juce::AffineTransform::scale ((float) bounds.getWidth() / (float) imageBounds.getWidth(),
                                                              (float) bounds.getHeight() / (float) imageBounds.getHeight()),
                                false);
        owner.paintPlayhead (g);
        if (ui::pictureLostMeanwhile (lossesSeen, lossRetries))
            owner.repaint();
    }

    bool invalidateAll() override // (JUCE's repaint() of the whole component comes here, not to invalidate())
    {
        if (! owner.playheadRepaintOnly && ! owner.scrollRepaintOnly)
            valid.clear();
        return true;
    }
    bool invalidate (const juce::Rectangle<int>& r) override
    {
        if (! owner.playheadRepaintOnly && ! owner.scrollRepaintOnly)
            valid.subtract (r);
        return true; // (the window repaints the area either way)
    }
    void releaseResources() override // (the Playlist was hidden: its pictures are freed)
    {
        image = {};
        spare = {};
        valid.clear();
    }

    // --cachetest: the picture's size in bytes (both images); whether its lanes are scaled (a zoom gesture)
    std::size_t memoryBytes() const
    {
        const auto bytes = [] (const juce::Image& i) { return i.isNull() ? (std::size_t) 0 : (std::size_t) i.getWidth() * (std::size_t) i.getHeight() * 4u; };
        return bytes (image) + bytes (spare);
    }
    bool hasScaledLanes() const { return lanesScaled; }

private:
    // (each step resamples the last: after 2 (a stretch of at most 1.44x from the last exact picture, at Ctrl+wheel's
    // 1.2 per notch) the lanes are painted exactly; round 174f, chosen by eye: 8 steps stretched names up to 4x)
    static constexpr int maxScaledSteps = 2;

    // A zoom step: column x of the picture (drawn at `from`) shows the same tick at a x + b in `to`
    struct Zoom
    {
        bool on = false;
        double a = 1.0, b = 0.0;
        juce::Rectangle<int> covered; // the part of the lanes the scaled picture fills
    };
    Zoom zoomFrom (const CacheView& from, const CacheView& to, juce::Rectangle<int> lanes) const
    {
        Zoom z;
        if (! owner.zoomGesture || owner.blocksShown || image.isNull() || from.pixelsPerTick <= 0.0 || scaledSteps >= maxScaledSteps
            || from.scrollY != to.scrollY || from.tracksHeight != to.tracksHeight || from.tracks != to.tracks || from.overlays || to.overlays
            || from.width != to.width || from.height != to.height || lanes.isEmpty())
            return z;
        z.a = to.pixelsPerTick / from.pixelsPerTick;
        if (z.a < 0.5 || z.a > 2.0)
            return z;
        // tickToX: x = headerWidth + (tick - scrollTicks) * pixelsPerTick
        z.b = owner.headerWidth + (from.scrollTicks - to.scrollTicks) * to.pixelsPerTick - owner.headerWidth * z.a;
        const auto left = z.a * lanes.getX() + z.b, right = z.a * lanes.getRight() + z.b;
        z.covered = lanes.getIntersection (juce::Rectangle<int>::leftTopRightBottom ((int) std::ceil (left), lanes.getY(), (int) std::floor (right),
                                                                                   lanes.getBottom()));
        z.on = true;
        return z;
    }

    // Paints `area` of the component as paint() does, without the playhead. A few rectangles (a scroll's strip, the
    // ruler, a hover): one plain rectangular clip each, which the GPU renderer clips for free and paint() culls by;
    // more: the area as one clip region (everything but `exceptFor`)
    void paintExactly (juce::Graphics& target, const juce::RectangleList<int>& area, const juce::RectangleList<int>& exceptFor)
    {
        const juce::ScopedValueSetter<bool> noPlayhead (owner.cachePainting, true);
        if (area.getNumRectangles() <= 4)
        {
            for (const auto& r : area)
            {
                const juce::Graphics::ScopedSaveState state (target);
                if (target.reduceClipRegion (r))
                    owner.paintEntireComponent (target, true);
            }
            return;
        }
        const juce::Graphics::ScopedSaveState state (target);
        for (const auto& r : exceptFor)
            target.excludeClipRegion (r);
        owner.paintEntireComponent (target, true);
    }

    static double area (const juce::RectangleList<int>& list)
    {
        double a = 0.0;
        for (const auto& r : list)
            a += (double) r.getWidth() * r.getHeight();
        return a;
    }

    PlaylistComponent& owner;
    juce::Image image, spare; // spare: the shifted / scaled copy is drawn into it, then they swap
    juce::RectangleList<int> valid;
    CacheView drawnView;
    bool lanesScaled = false; // the lanes are a zoom gesture's scaled picture
    int scaledSteps = 0;
    double lastLargeMs = 0.0;
    bool softwareImage = false; // the pictures are software images (the window is drawn without the GPU)
    int lossesSeen = 0;         // ui::pictureLossCount() when the picture was last known good
    int lossRetries = 0;
    juce::String noted; // ui::notePicture's memory
};

// What the cached picture depends on beyond what repaints say (round 174e2)
PlaylistComponent::CacheView PlaylistComponent::cacheView() const
{
    return { scrollTicks, scrollY, pixelsPerTick, trackTops.empty() ? 0 : trackTops.back(), (int) trackTops.size(),
             ripple || engine.getSong().performanceMode, getWidth(), getHeight() };
}

// The picture of view `from` shown as `to` by shifting it: the area that moves and by how much (whole physical pixels).
// Horizontal: the ruler and the lanes (the headers stay); vertical: the headers and the lanes (the ruler stays).
// Empty area: not a plain scroll (zoom, resize, track heights, both directions at once, the corner tags).
PlaylistComponent::CacheShift PlaylistComponent::cacheShift (const CacheView& from, const CacheView& to, float scale) const
{
    if (from.pixelsPerTick != to.pixelsPerTick || from.tracksHeight != to.tracksHeight || from.tracks != to.tracks
        || from.overlays || to.overlays || from.width != to.width || from.height != to.height)
        return {};
    const auto grid = gridArea();
    const auto dxExact = (from.scrollTicks - to.scrollTicks) * to.pixelsPerTick;
    const auto dx = (int) std::lround (dxExact), dy = from.scrollY - to.scrollY;
    const auto whole = [scale] (int d) { return std::abs ((float) d * scale - (float) juce::roundToInt ((float) d * scale)) < 0.01f; };
    if (std::abs (dxExact - dx) > 0.01 || (dx != 0 && dy != 0) || ! whole (dx) || ! whole (dy))
        return {};
    CacheShift shift;
    if (dx != 0)
    {
        shift.area = { headerWidth, rulerTop, grid.getRight() - headerWidth, grid.getBottom() - rulerTop };
        shift.alsoChanged = { headerWidth, grid.getBottom(), grid.getWidth(), getHeight() - grid.getBottom() }; // (the pattern blocks)
    }
    else
        shift.area = { 0, headerHeight, grid.getRight(), grid.getHeight() };
    shift.dx = dx;
    shift.dy = dy;
    if (std::abs (dx) >= shift.area.getWidth() || std::abs (dy) >= shift.area.getHeight())
        return {};
    return shift;
}

// --cachetest (round 174f): the picture the cache shows against an exact paint of the same view, after the cases that
// reuse it (a zoom gesture and its settle, scrolls by whole pixels, an edit). Off screen, at scale 1. Every exact case
// must match to the pixel; the zoom gesture's frames are scaled (their difference is reported, the rest of the view
// outside the lanes must match).
juce::String PlaylistComponent::cacheSelfTest (const std::function<void()>& waitForWaveforms, const juce::File& dumpFolder)
{
    auto* cache = dynamic_cast<PlayheadCache*> (getCachedComponentImage());
    if (cache == nullptr)
        return "cache: off (LYNX_PLAYLIST_CACHE=0)\nresult=SKIPPED\n";
    const auto w = getWidth(), h = getHeight();
    auto render = [w, h] (auto&& paintFn) {
        juce::Image im (juce::Image::RGB, w, h, true);
        {
            juce::Graphics g (im);
            paintFn (g);
        }
        return im;
    };
    auto exact = [&] { return render ([this] (juce::Graphics& g) { paintEntireComponent (g, true); }); };
    auto cached = [&] { return render ([cache] (juce::Graphics& g) { cache->paint (g); }); };
    struct Diff
    {
        int pixels = 0, worst = 0;
        juce::Rectangle<int> box;
    };
    auto diff = [] (const juce::Image& a, const juce::Image& b, juce::Rectangle<int> area, juce::Rectangle<int> except) {
        Diff d;
        const juce::Image::BitmapData pa (a, juce::Image::BitmapData::readOnly), pb (b, juce::Image::BitmapData::readOnly);
        for (int y = area.getY(); y < area.getBottom(); ++y)
            for (int x = area.getX(); x < area.getRight(); ++x)
            {
                if (except.contains (x, y))
                    continue;
                const auto ca = pa.getPixelColour (x, y), cb = pb.getPixelColour (x, y);
                const auto m = juce::jmax (std::abs ((int) ca.getRed() - (int) cb.getRed()), std::abs ((int) ca.getGreen() - (int) cb.getGreen()),
                                           std::abs ((int) ca.getBlue() - (int) cb.getBlue()));
                if (m > 0)
                {
                    ++d.pixels;
                    d.box = d.box.isEmpty() ? juce::Rectangle<int> (x, y, 1, 1) : d.box.getUnion ({ x, y, 1, 1 });
                }
                d.worst = juce::jmax (d.worst, m);
            }
        return d;
    };
    juce::String out;
    auto failures = 0;
    auto dumpedScaled = false;
    const auto all = getLocalBounds();
    auto check = [&] (const juce::String& name, bool mustMatch, juce::Rectangle<int> except = {}) {
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        const auto viaCache = cached();
        const auto cacheMs = juce::Time::getMillisecondCounterHiRes() - t0;
        const auto t1 = juce::Time::getMillisecondCounterHiRes();
        const auto reference = exact();
        const auto exactMs = juce::Time::getMillisecondCounterHiRes() - t1;
        const auto d = diff (viaCache, reference, all, except);
        // (one step of 255 is allowed: a view scrolled by a whole number of pixels puts shapes at positions that differ
        // from the shifted ones by float rounding, ~1e-12 px, which can move an anti-aliased edge's coverage by one step)
        const auto ok = d.worst <= 1;
        failures += mustMatch && ! ok ? 1 : 0;
        out << name << ": " << (mustMatch ? (ok ? (d.pixels == 0 ? "match" : "match (1-step rounding)") : "MISMATCH") : (d.pixels == 0 ? "match" : "scaled")) << ", " << d.pixels << " px differ (max "
            << d.worst << (ok ? juce::String() : ", in " + d.box.toString()) << "), via cache " << juce::String (cacheMs, 2) << " ms, exact "
            << juce::String (exactMs, 2) << " ms\n";
        // (both pictures, to look at: a mismatch, and the first scaled zoom frame)
        const auto dump = mustMatch ? ! ok : (d.pixels > 0 && ! dumpedScaled);
        if (! mustMatch && dump)
            dumpedScaled = true;
        if (dump && dumpFolder != juce::File())
            for (const auto& [suffix, im] : { std::pair<const char*, const juce::Image*> { "cache", &viaCache }, { "exact", &reference } })
            {
                juce::PNGImageFormat png;
                juce::FileOutputStream stream (dumpFolder.getChildFile ("cachetest-" + (mustMatch ? juce::String (failures) : juce::String ("zoomframe")) + "-" + suffix + ".png"));
                if (stream.openedOk())
                {
                    stream.setPosition (0);
                    stream.truncate();
                    png.writeImageToStream (*im, stream);
                }
            }
    };

    // (the waveforms are analysed in the background: one paint asks for them, then they are waited for)
    exact();
    if (waitForWaveforms)
        waitForWaveforms();
    repaint();
    check ("first picture", true);
    check ("unchanged view (blit only)", true);

    // a zoom gesture: four steps in, two out (scaled, the lanes only), then still
    const auto anchor = (double) gridArea().getWidth() * 0.4;
    auto zoomStep = [&] (double factor) {
        const auto zoomed = juce::jlimit (4.0 / (4 * ppq), 800.0 / (4 * ppq), pixelsPerTick * factor);
        scrollTicks = gesture::zoomAnchored (scrollTicks, pixelsPerTick, zoomed, anchor);
        pixelsPerTick = zoomed;
        updateScrollBars();
        repaintZoomed();
    };
    for (auto f : { 1.2, 1.2, 1.2, 1.0 / 1.2, 1.0 / 1.2 })
    {
        zoomStep (f);
        check ("zoom step x" + juce::String (f, 3) + " (outside the lanes)", true, gridArea());
        check ("zoom step x" + juce::String (f, 3) + " (lanes scaled)", false);
    }
    settleZoom();
    check ("zoom settled", true);
    // past 2x in one gesture: the lanes are painted again on the way
    for (int i = 0; i < 6; ++i)
    {
        zoomStep (1.0 / 1.2);
        check ("zoom out step " + juce::String (i + 1) + " (outside the lanes)", true, gridArea());
    }
    settleZoom();
    check ("zoom out settled", true);

    // a plain wheel moves the view up and down, wherever a zoom left it sideways: at 0, a whole pixel or between two
    // pixels, the glide must not touch it (between two, it used to flip by a pixel's fraction every frame, and each
    // frame painted everything). Then a sideways glide into the end of the song has to finish.
    auto runGlide = [&] {
        int moves = 0, frames = 0;
        for (auto last = scrollTicks; gliding && frames < 400; ++frames)
        {
            glideFrame();
            moves += scrollTicks != last ? 1 : 0;
            last = scrollTicks;
        }
        return std::pair { moves, frames };
    };
    for (const auto pixels : { 0.0, 130.0, 130.8 })
    {
        gliding = false;
        scrollTicks = pixels / pixelsPerTick;
        updateScrollBars();
        const auto before = scrollTicks;
        glideBy (0.0, 2.0 * defaultRowHeight(), false);
        const auto [moves, frames] = runGlide();
        out << "wheel glide, view at " << juce::String (pixels, 1) << " px: " << moves << " sideways moves in " << frames << " frames\n";
        if (moves != 0 || scrollTicks != before || gliding)
        {
            ++failures;
            out << "FAIL wheel glide moved the view sideways\n";
        }
    }
    gliding = false;
    scrollY = 0;
    glideBy (1.0e9, 0.0, false);
    if (const auto [moves, frames] = runGlide(); gliding)
    {
        ++failures;
        out << "FAIL sideways glide into the end did not finish: " << moves << " moves in " << frames << " frames\n";
    }
    gliding = false;
    scrollTicks = 0.0;
    updateScrollBars();
    repaint();
    check ("after the glides", true);

    // scrolls by whole pixels (the picture shifted, the strip painted)
    scrollTicks = juce::jmax (0.0, scrollTicks + 37.0 / pixelsPerTick);
    updateScrollBars();
    repaintScrolled();
    check ("scroll right 37 px", true);
    scrollTicks = juce::jmax (0.0, scrollTicks - 11.0 / pixelsPerTick);
    updateScrollBars();
    repaintScrolled();
    check ("scroll left 11 px", true);
    if (tracksHeight() > gridArea().getHeight() + 30)
    {
        scrollY += 23;
        updateScrollBars();
        repaintScrolled();
        check ("scroll down 23 px", true);
    }
    // a partial repaint (a hover, an edit): its area painted again
    repaint (gridArea().withSizeKeepingCentre (200, 80));
    check ("partial repaint", true);
    repaint();
    check ("whole repaint", true);
    out << "picture memory: " << juce::String ((double) cache->memoryBytes() / (1024.0 * 1024.0), 1) << " MB\n";
    setVisible (false); // (JUCE releases the cached pictures of a hidden component)
    out << "hidden: picture memory " << juce::String ((double) cache->memoryBytes() / (1024.0 * 1024.0), 1) << " MB\n";
    setVisible (true);
    out << "failures: " << failures << "\nresult=" << (failures == 0 ? "OK" : "FAIL") << "\n";
    return out;
}

// A repaint after the view scrolled: the cached picture is shifted instead of painted again (round 174e2)
void PlaylistComponent::repaintScrolled()
{
    scrollRepaintOnly = playheadOverlay;
    repaint();
    scrollRepaintOnly = false;
}

// A repaint after a step of a continuous zoom (Ctrl+wheel, the overview's ends, the zoom keys): the cache shows the
// lanes scaled from its picture until the zoom has been still for zoomSettleMs, then they are painted exactly (round 174f)
void PlaylistComponent::repaintZoomed()
{
    static const bool scaled = [] { const auto* v = std::getenv ("LYNX_ZOOM_SCALE"); return v == nullptr || juce::String (v) != "0"; }(); // (0: A/B)
    if (playheadOverlay && scaled)
    {
        zoomGesture = true;
        zoomSettle.startTimer (zoomSettleMs);
    }
    repaintScrolled();
}

void PlaylistComponent::settleZoom()
{
    zoomSettle.stopTimer();
    if (! zoomGesture)
        return;
    zoomGesture = false;
    zoomEndMs = juce::Time::getMillisecondCounterHiRes();
    repaint (gridArea()); // (the lanes, exactly; the rest of the picture already is)
}

bool PlaylistComponent::zoomRecent (double nowMs) const
{
    return zoomGesture || nowMs - zoomEndMs < 400.0;
}

juce::Rectangle<int> PlaylistComponent::rulerArea() const
{
    return { headerWidth, rulerTop, gridArea().getWidth(), timelineHeight };
}

PlaylistComponent::PlaylistComponent (AudioEngine& e)
    : engine (e)
{
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);

    for (auto* bar : { &hBar, &vBar })
    {
        bar->addListener (this);
        bar->setAutoHide (false);
        addAndMakeVisible (bar);
    }

    // the overview strip is the horizontal scroller (FL's Playlist has its preview bar on top)
    hBar.setVisible (false);
    overview.paintContent = [this] (juce::Graphics& g, juce::Rectangle<float> area, double total) { paintOverview (g, area, total); };
    overview.onChange = [this] (double start, double length) {
        const auto width = (double) gridArea().getWidth();
        pixelsPerTick = juce::jlimit (4.0 / (4 * ppq), 800.0 / (4 * ppq), width / juce::jmax (1.0, length));
        scrollTicks = juce::jmax (0.0, start);
        updateScrollBars();
        repaintZoomed(); // (dragging the overview's window or its ends: a continuous scroll / zoom)
    };
    overview.onShowAll = [this] { zoomAll(); };
    addAndMakeVisible (overview);

    // FL's legacy Pattern Blocks (round 170f): shown under the clip tracks while the project's option is on
    blocksView = std::make_unique<PatternBlocksView> (engine, *this);
    addChildComponent (*blocksView);
    videoLanes = std::make_unique<VideoLanesView> (engine, *this);
    addChildComponent (*videoLanes);

    grid::fillEditorBox (snapBox);
    snapBox.setTooltip (tr ("Playlist snap (magnet): \"Main\" follows the snap in the toolbar, or pick this window's own; hold Alt while dragging to ignore it"));
    ui::setQuickTip (snapBox, tr ("Playlist snap"));
    snapBox.setWantsKeyboardFocus (false);
    snapBox.setMouseClickGrabsKeyboardFocus (false);
    snapBox.onChange = [this] { repaint(); };
    addAndMakeVisible (snapBox);
    tools.onChange = [this] (EditTool) { updateCursor (juce::ModifierKeys::currentModifiers); };
    setOpaque (true); // paints every pixel: nothing behind it is redrawn while scrolling
    if (const auto* v = std::getenv ("LYNX_PLAYLIST_CACHE"); v == nullptr || juce::String (v) != "0")
    {
        setCachedComponentImage (new PlayheadCache (*this)); // (owned by the component)
        playheadOverlay = true;
    }
    addAndMakeVisible (tools);

    // Follow (FL: "Auto scrolling"): the view pages along with the playhead in Song mode; off, it stays put
    followButton.setOnColour (theme::colour::bg6);
    followButton.setIconColourWhenOn (theme::colour::accent);
    followButton.setFlat (true);
    followButton.setClickingTogglesState (false);
    followButton.setToggleState (EditorGlobals::get().isFollowing (false), juce::dontSendNotification);
    followButton.setTooltip (tr ("Follow: the Playlist scrolls along with the playhead while the song plays; off, you can look around freely"));
    ui::setQuickTip (followButton, tr ("Follow the playhead"));
    followButton.onClick = [this] { runAction ("playlist.follow"); };
    followButton.setWantsKeyboardFocus (false);
    followButton.setMouseClickGrabsKeyboardFocus (false);
    addAndMakeVisible (followButton);
    // FL's STRETCH switch (ClipStretchBtn [DFM]: "Stretch audio when resizing audio clips", Shift+M; round 168f)
    stretchButton.setOnColour (theme::colour::bg6);
    stretchButton.setIconColourWhenOn (theme::colour::accent);
    stretchButton.setFlat (true);
    stretchButton.setClickingTogglesState (false);
    stretchButton.setToggleState (EditorGlobals::get().stretchOnResize, juce::dontSendNotification);
    stretchButton.setTooltip (tr ("Stretch: resizing an audio clip stretches its audio instead of cutting it (Shift+M)"));
    ui::setQuickTip (stretchButton, tr ("Stretch audio when resizing audio clips"));
    stretchButton.onClick = [this] { runAction ("playlist.stretchMode"); };
    stretchButton.setWantsKeyboardFocus (false);
    stretchButton.setMouseClickGrabsKeyboardFocus (false);
    addAndMakeVisible (stretchButton);

    {
        auto button = std::make_unique<AddTrackButton>();
        button->setTooltip (tr ("Add instrument track / Add audio track"));
        button->onMenu = [this] {
            buildAddTrackMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (addTrackButton.get()));
        };
        button->onAddAudioTrack = [this] { addAudioTrackNow(); };
        button->onMorePlugins = [this] { morePluginsForTrack (-1); };
        addTrackButton = std::move (button);
        addAndMakeVisible (*addTrackButton);
    }

    arrangementBox.setTooltip (tr ("Arrangement (as in FL, a project can have several)"));
    arrangementBox.setWantsKeyboardFocus (false);
    arrangementBox.setMouseClickGrabsKeyboardFocus (false);
    arrangementBox.onChange = [this] { arrangementChosen(); };
    addAndMakeVisible (arrangementBox);
    refreshArrangementBox();
    refreshTrackLayout();

    engine.addChangeListener (this);
    EditorGlobals::get().addChangeListener (this);
    ui::WaveformCache::getInstance()->addChangeListener (this); // a waveform became ready, or the settings changed
}

namespace
{
enum ArrangementAction
{
    newArrangement = 1001,
    cloneArrangement,
    renameArrangementAction,
    removeArrangementAction,
    manageArrangements
};
} // namespace

void PlaylistComponent::refreshArrangementBox()
{
    const auto& song = engine.getSong();
    juce::StringArray names;

    for (const auto& a : song.arrangements)
        names.add (juce::String::fromUTF8 (a.name.c_str()));

    // only rebuilds when something changed (rebuilding closes the open menu)
    bool same = arrangementBox.getNumItems() == names.size() + 4;
    for (int i = 0; same && i < names.size(); ++i)
        same = arrangementBox.getItemText (i) == names[i];

    if (! same)
    {
        arrangementBox.clear (juce::dontSendNotification);

        for (int i = 0; i < names.size(); ++i)
            arrangementBox.addItem (names[i], i + 1);

        arrangementBox.addSeparator();
        arrangementBox.addItem (tr ("New arrangement"), newArrangement);
        arrangementBox.addItem (tr ("Clone this one"), cloneArrangement);
        arrangementBox.addItem (tr ("Rename..."), renameArrangementAction);
        arrangementBox.addItem (tr ("Remove this one"), removeArrangementAction);
        arrangementBox.addSeparator();
        arrangementBox.addItem (tr ("Arrangements and merging..."), manageArrangements);
        arrangementBox.setItemEnabled (removeArrangementAction, names.size() > 1);
    }

    arrangementBox.setSelectedId (song.currentArrangement + 1, juce::dontSendNotification);
}

void PlaylistComponent::arrangementChosen()
{
    const auto id = arrangementBox.getSelectedId();
    const auto& song = engine.getSong();
    setSelection ({});

    if (id >= 1 && id <= (int) song.arrangements.size())
    {
        if (id - 1 != song.currentArrangement)
            engine.editSong ([index = id - 1] (Song& s) { s.selectArrangement (index); }, false);
        return;
    }

    switch (id)
    {
        case newArrangement:
            engine.editSong ([] (Song& s) { s.addArrangement ("Arrangement " + std::to_string (s.arrangements.size() + 1)); });
            break;
        case cloneArrangement:
            engine.editSong ([] (Song& s) {
                s.addArrangement (s.arrangements[(std::size_t) s.currentArrangement].name + " (copy)", true);
            });
            break;
        case renameArrangementAction:
            renameArrangement();
            break;
        case manageArrangements:
            showArrangements (engine);
            break;
        case removeArrangementAction:
            engine.editSong ([] (Song& s) { s.removeArrangement (s.currentArrangement); });
            break;
        default:
            break;
    }

    refreshArrangementBox();
}

void PlaylistComponent::renameArrangement()
{
    const auto index = engine.getSong().currentArrangement;
    auto* window = new juce::AlertWindow (tr ("Rename arrangement"), {}, juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("name", juce::String::fromUTF8 (engine.getSong().arrangements[(std::size_t) index].name.c_str()));
    window->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    window->enterModalState (true, juce::ModalCallbackFunction::create ([this, index, window] (int result) {
                                 const auto name = window->getTextEditorContents ("name").trim().toStdString();

                                 if (result == 1 && ! name.empty() && index < (int) engine.getSong().arrangements.size())
                                     engine.editSong ([index, name] (Song& s) { s.arrangements[(std::size_t) index].name = name; });
                             }),
                             true);
}

PlaylistComponent::~PlaylistComponent()
{
    EditorGlobals::get().removeChangeListener (this);
    engine.removeChangeListener (this);
    if (auto* cache = ui::WaveformCache::getInstanceWithoutCreating())
        cache->removeChangeListener (this);
}

juce::Colour PlaylistComponent::patternColour (const Song& song, int pattern)
{
    if (pattern >= 0 && pattern < (int) song.patterns.size() && song.patterns[(std::size_t) pattern].colour != 0)
        return juce::Colour (song.patterns[(std::size_t) pattern].colour);

    // hues spread by the golden ratio: neighboring patterns get clearly different colors
    const auto hue = std::fmod (0.08f + (float) pattern * 0.618034f, 1.0f);
    return juce::Colour::fromHSV (hue, 0.45f, 0.75f, 1.0f);
}

juce::Rectangle<int> PlaylistComponent::gridArea() const
{
    return getLocalBounds().withTrimmedLeft (headerWidth).withTrimmedTop (tracksTop())
        .withTrimmedRight (scrollBarSize).withTrimmedBottom (blocksSectionHeight());
}

int PlaylistComponent::blocksSectionHeight() const
{
    if (! engine.getSong().patternBlocksOn)
        return 0;
    const auto available = getHeight() - tracksTop();
    const auto least = PatternBlocksView::dividerHeight + 3 * PatternBlocksView::rowHeight;
    const auto most = juce::jmax (least, available - 2 * trackHeight); // the clip tracks keep two rows
    // by default a third of the grid, or less when every row (and a spare one) fits in less
    const auto rows = PatternBlocksView::dividerHeight + ((int) engine.getSong().patterns.size() + 1) * PatternBlocksView::rowHeight;
    const auto wanted = blocksHeight > 0 ? blocksHeight : juce::jmin (available / 3, rows);
    return juce::jlimit (least, most, wanted);
}

int PlaylistComponent::videoSectionHeight() const
{
    const auto natural = VideoLanesView::contentHeight (engine.getSong());
    if (natural <= 0)
        return 0;
    // (the clip tracks keep two rows; with the Pattern Blocks section too, the video lanes take at most half)
    const auto available = getHeight() - headerHeight;
    const auto least = VideoLanesView::dividerHeight + 20;
    const auto most = juce::jmax (least, engine.getSong().patternBlocksOn ? available / 2 : available - 2 * trackHeight);
    return juce::jlimit (least, most, videoHeight > 0 ? videoHeight : natural);
}

void PlaylistComponent::setVideoHeight (int height)
{
    videoHeight = juce::jmax (0, height);
    resized();
    repaint();
}

void PlaylistComponent::setBlocksHeight (int height)
{
    blocksHeight = juce::jmax (1, height);
    resized();
    repaint();
}

void PlaylistComponent::scrollTicksBy (double ticks)
{
    scrollTicks = juce::jmax (0.0, scrollTicks + ticks);
    updateScrollBars();
    repaint();
}

void PlaylistComponent::togglePatternBlocks()
{
    const auto& song = engine.getSong();
    if (! song.patternBlocksOn)
    {
        engine.editSong ([] (Song& s) { s.patternBlocksOn = true; });
        return;
    }
    auto any = false;
    for (const auto& a : song.arrangements)
        any = any || ! a.blocks.empty();
    if (song.blocks.empty() && ! any)
    {
        engine.editSong ([] (Song& s) { s.patternBlocksOn = false; });
        return;
    }
    // blocks that stay hidden would still be in the project but silent: FL's own way out was to turn them into clips
    auto options = juce::MessageBoxOptions()
                       .withIconType (juce::MessageBoxIconType::QuestionIcon)
                       .withTitle (tr ("Pattern blocks"))
                       .withMessage (tr ("This project has pattern blocks. Turn them into clips (one track per pattern) so they keep playing, or delete them?"))
                       .withButton (tr ("Turn them into clips"))
                       .withButton (tr ("Delete them"))
                       .withButton (tr ("Cancel"))
                       .withAssociatedComponent (this);
    juce::AlertWindow::showAsync (options, [safe = juce::Component::SafePointer<PlaylistComponent> (this)] (int result) {
        if (safe == nullptr || result == 0)
            return;
        safe->engine.editSong ([result] (Song& s) {
            if (result == 1)
                patternblocks::toClipsInEveryArrangement (s);
            s.forEachBlockList ([] (std::vector<PatternBlock>& list) { list.clear(); });
            s.patternBlocksOn = false;
        });
    });
}

void PlaylistComponent::turnBlocksIntoClips()
{
    if (engine.getSong().blocks.empty())
    {
        ui::postHint ({ tr ("No pattern blocks to turn into clips"), {} }, 3000);
        return;
    }
    setSelection ({});
    engine.editSong ([] (Song& s) { patternblocks::toClips (s); });
}

double PlaylistComponent::tickToX (double tick) const { return headerWidth + (tick - scrollTicks) * pixelsPerTick; }
double PlaylistComponent::xToTick (int x) const { return (x - headerWidth) / pixelsPerTick + scrollTicks; }
int PlaylistComponent::heightOf (int track) const
{
    const auto& tracks = engine.getSong().playlistTracks;
    if (noteedit::isFoldedAway (engine.getSong(), track))
        return foldedHeight;
    if (arrange::isFolded (engine.getSong(), track)) // round 190cg: a folded track is its title bar
        return liveFoldedHeight;
    const auto h = track >= 0 && track < (int) tracks.size() ? tracks[(std::size_t) track].height : 0;
    // "Lock to this size" (FL +0x14, round 168f): the row keeps its px height whatever the vertical zoom
    if (const auto locked = track >= 0 && track < (int) tracks.size() ? tracks[(std::size_t) track].lockedHeight : -1; locked > 0)
        return polish::clampTrackHeight (locked);
    return scaledHeight (h > 0 ? polish::clampTrackHeight (h) : (simpleHeaders ? simpleTrackHeight : trackHeight));
}

juce::Rectangle<int> PlaylistComponent::trackZoomHandle() const
{
    // the corner above the vertical scroll bar, beside the overview and the ruler (where FL keeps it)
    return { getWidth() - scrollBarSize, toolbarHeight, scrollBarSize, headerHeight - toolbarHeight };
}

void PlaylistComponent::setTrackZoom (double zoom, int anchorY)
{
    zoom = juce::jlimit (0.5, 4.0, zoom);
    if (std::abs (zoom - trackZoom) < 1.0e-4)
        return;

    // the track under `anchorY` stays where it is
    anchorY = juce::jlimit (tracksTop(), juce::jmax (tracksTop(), gridArea().getBottom() - 1), anchorY);
    const auto track = yToTrack (anchorY);
    const auto within = heightOf (track) > 0 ? (double) (anchorY - trackToY (track)) / heightOf (track) : 0.0;
    trackZoom = zoom;
    refreshTrackLayout();
    const auto top = trackToY (track) + scrollY - tracksTop(); // the track's top in the content
    scrollY = juce::jmax (0, top + (int) std::lround (within * heightOf (track)) - (anchorY - tracksTop()));
    updateScrollBars();
    repaint();
    ui::postHint ({ tr ("Track height (vertical zoom)"), juce::String (juce::roundToInt (trackZoom * 100.0)) + "%", false, false });
}

void PlaylistComponent::refreshTrackLayout()
{
    const auto count = numTracks();
    trackTops.assign ((std::size_t) count + 1, 0);

    for (int t = 0; t < count; ++t)
        trackTops[(std::size_t) t + 1] = trackTops[(std::size_t) t] + heightOf (t) + laneBlockHeight (t); // (+ its lanes: round 177b)
}

int PlaylistComponent::tracksHeight() const
{
    return trackTops.empty() ? numTracks() * defaultRowHeight() : trackTops.back();
}

int PlaylistComponent::trackToY (int track) const
{
    // past the cached rows (the list grew during a drag): default heights
    const auto last = (int) trackTops.size() - 1;
    const auto top = trackTops.empty() ? track * defaultRowHeight()
                     : track <= last   ? trackTops[(std::size_t) juce::jmax (0, track)]
                                       : trackTops.back() + (track - last) * defaultRowHeight();
    return tracksTop() + top - scrollY;
}

int PlaylistComponent::yToTrack (int y) const
{
    const auto offset = y - tracksTop() + scrollY;

    if (trackTops.size() < 2)
        return juce::jlimit (0, numTracks() - 1, offset / defaultRowHeight());

    const auto it = std::upper_bound (trackTops.begin(), trackTops.end(), offset);
    const auto index = (int) (it - trackTops.begin()) - 1;
    return juce::jlimit (0, numTracks() - 1, index);
}

int PlaylistComponent::dividerAt (juce::Point<int> p) const
{
    // the bottom edge of a track header (4 px band) resizes that track
    if (p.x >= headerWidth || p.y < tracksTop() || p.y >= gridArea().getBottom())
        return -1;

    const auto track = yToTrack (p.y);

    for (auto t : { track - 1, track })
        if (t >= 0 && ! noteedit::isFoldedAway (engine.getSong(), t) && std::abs (p.y - (trackToY (t) + heightOf (t))) <= 3)
            return t;

    return -1;
}

void PlaylistComponent::setTrackHeights (std::vector<int> tracks, int height)
{
    engine.editSong ([tracks, height] (Song& s) {
        for (auto t : tracks)
        {
            if (t < 0)
                continue;
            if ((int) s.playlistTracks.size() <= t)
                s.playlistTracks.resize ((std::size_t) t + 1);
            s.playlistTracks[(std::size_t) t].height = height == trackHeight ? 0 : polish::clampTrackHeight (height);
            pltracks::unlockSize (s.playlistTracks[(std::size_t) t]); // a new size unlocks it (FL's Size menu)
        }
    });
    refreshTrackLayout();
    updateScrollBars();
    repaint();
}

int PlaylistComponent::numTracks() const
{
    int highest = 0;

    for (const auto& c : engine.getSong().playlist)
        highest = juce::jmax (highest, c.track);

    // Options › Mixer › "Playlist tracks shown" (round 163; 16 by default): at least that many, and always a few free
    // tracks under the last clip
    const auto least = juce::jlimit (1, 500, newProjectDefaults().playlistTracks);
    return juce::jlimit (least, 500, highest + 1 + juce::jlimit (1, 7, least / 2));
}

std::int64_t PlaylistComponent::gridTicks() const
{
    const auto bar = (std::int64_t) ppq * juce::jmax (1, engine.getSong().beatsPerBar);
    const auto& option = grid::resolve (snapBox);

    switch (option.kind)
    {
        case grid::Kind::none: return 1;
        case grid::Kind::ticks: return option.ticks;
        case grid::Kind::bar: return bar;
        case grid::Kind::screen:
        case grid::Kind::nearest: break;
    }

    // FL's Line step at this zoom (round 190ax; it was noteedit::screenStep's: a beat where FL steps half a beat, and
    // quarter steps zoomed in where FL stays on steps)
    return noteedit::flLineStep (pixelsPerTick, engine.getSong().beatsPerBar);
}

std::vector<std::int64_t> PlaylistComponent::snapCandidates() const
{
    // edges of the other clips ("Nearest note" in the Playlist snaps to clip edges)
    std::vector<std::int64_t> out;
    const auto& playlist = engine.getSong().playlist;

    for (int i = 0; i < (int) playlist.size(); ++i)
        if (std::find (dragIndices.begin(), dragIndices.end(), i) == dragIndices.end())
        {
            out.push_back (playlist[(std::size_t) i].start);
            out.push_back (playlist[(std::size_t) i].end());
        }

    return out;
}

std::int64_t PlaylistComponent::snap (double tick, bool enabled) const
{
    if (! enabled)
        return (std::int64_t) std::floor (tick);

    const auto& option = grid::resolve (snapBox);
    const auto step = gridTicks();

    if (option.kind == grid::Kind::nearest)
        return noteedit::snapNearest (tick, snapCandidates(), 10.0 / pixelsPerTick, step);

    // whole bars respect time signature changes
    if (step == (std::int64_t) ppq * juce::jmax (1, engine.getSong().beatsPerBar))
        return engine.getSong().barAt ((std::int64_t) std::max (0.0, std::floor (tick))).tick;

    return noteedit::snapDown (tick, step);
}

void PlaylistComponent::updateScrollBars()
{
    const auto& song = engine.getSong();
    const auto grid = gridArea();
    const auto bar = (double) ppq * song.beatsPerBar;
    const auto total = juce::jmax ((double) song.songLength() + 16 * bar, grid.getWidth() / pixelsPerTick);

    hBar.setRangeLimits (0.0, total, juce::dontSendNotification);
    hBar.setCurrentRange (scrollTicks, grid.getWidth() / pixelsPerTick, juce::dontSendNotification);
    vBar.setRangeLimits (0.0, (double) tracksHeight(), juce::dontSendNotification);
    vBar.setCurrentRange (scrollY, grid.getHeight(), juce::dontSendNotification);
    scrollTicks = hBar.getCurrentRangeStart();
    scrollY = (int) vBar.getCurrentRangeStart();
    overview.minLength = grid.getWidth() / (800.0 / (4 * ppq));
    overview.setRange (total, scrollTicks, grid.getWidth() / pixelsPerTick);
}

void PlaylistComponent::setTitleBarInsets (int left, int right)
{
    if (left != titleInsetLeft || right != titleInsetRight)
    {
        titleInsetLeft = left;
        titleInsetRight = right;
        resized();
    }
}

void PlaylistComponent::resized()
{
    const auto h = toolbarHeight - 6;
    arrangementBox.setBounds (4 + titleInsetLeft, 3, juce::jmin (150, getWidth() / 5), h);
    tools.setBounds (arrangementBox.getRight() + 8, 3, tools.getIdealWidth (h), h);
    snapBox.setBounds (tools.getRight() + 8, 3, 128, h);
    followButton.setBounds (snapBox.getRight() + 6, 3, h, h);
    stretchButton.setBounds (followButton.getRight() + 2, 3, h, h);
    // beside the clip grid the Playlist is narrow: what would run under the panel's title buttons goes (the inset keeps
    // 4 px of air before them). Anywhere else the toolbar stays exactly as it was.
    const auto toolbarEnd = getWidth() - titleInsetRight + 4;
    if (toolbarFitsNarrow && tools.getRight() > toolbarEnd)
        tools.setSize (juce::jmax (0, toolbarEnd - tools.getX()), h);
    for (auto* c : std::initializer_list<juce::Component*> { &snapBox, &followButton, &stretchButton })
        c->setVisible (! toolbarFitsNarrow || c->getRight() <= toolbarEnd);
    overview.setBounds (headerWidth, toolbarHeight, getWidth() - headerWidth - scrollBarSize, overviewHeight);
    // FL's "+" (21 x 18 at 7 px from the left of the header panel), on the ruler's row; FL's headers only
    if (addTrackButton != nullptr)
    {
        addTrackButton->setBounds (7, rulerTop + (timelineHeight - 18) / 2, 21, 18);
        addTrackButton->setVisible (! simpleHeaders);
    }
    const auto blocks = blocksSectionHeight();
    blocksShown = blocks > 0;
    vBar.setBounds (getWidth() - scrollBarSize, tracksTop(), scrollBarSize, getHeight() - tracksTop() - blocks);
    blocksView->setBounds (0, getHeight() - blocks, getWidth(), blocks);
    blocksView->setVisible (blocksShown);
    const auto video = videoSectionHeight();
    videoLanes->setBounds (0, headerHeight, getWidth(), video);
    videoLanes->setVisible (video > 0);
    updateScrollBars();

    if (fitSongPending)
        showSongAfterOpen();
}

void PlaylistComponent::showSongAfterOpen()
{
    const auto width = gridArea().getWidth();
    fitSongPending = width < 100;

    if (fitSongPending)
        return;

    scrollY = 0;
    scrollTicks = 0.0;
    const auto length = (double) engine.getSong().songLength();

    if (length > width / pixelsPerTick)
        zoomAll(); // (also updates the scroll bars)
    else
    {
        updateScrollBars();
        repaint();
    }
}

void PlaylistComponent::paint (juce::Graphics& g)
{
    const uitrace::Scope trace ("PlaylistComponent paint", true, g.getClipBounds());
    if (trackTops.size() != (std::size_t) numTracks() + 1) // the track list grew or shrank since the last change message
        refreshTrackLayout();

    const auto& song = engine.getSong();
    const auto grid = gridArea();
    const auto bar = (std::int64_t) ppq * song.beatsPerBar;
    const auto tracks = numTracks();

    // the overview follows the clips (edits, undo, arrangement switches) and the view
    {
        auto signature = song.playlist.size() * 1000003u + (std::size_t) song.currentArrangement;
        for (const auto& c : song.playlist)
            signature = signature * 31u + (std::size_t) (c.start * 131 + c.length * 7 + c.track * 257 + c.pattern + (c.muted ? 1 : 0));
        if (signature != overviewSignature)
        {
            overviewSignature = signature;
            overview.repaint();
        }
        updateScrollBars();
    }

    using namespace theme;
    g.fillAll (colour::bg1);

    // --- tracks and grid ---------------------------------------------------
    {
        const uitrace::Scope traceGrid ("Playlist lanes+clips", false);
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (grid);
        const auto paintedRows = g.getClipBounds();

        for (int t = 0; t < tracks; ++t)
        {
            const auto y = trackToY (t);
            const auto trackHeight = heightOf (t);
            const auto rowsHeight = trackHeight + laneBlockHeight (t);

            if (y > grid.getBottom() || y + rowsHeight < grid.getY() || y > paintedRows.getBottom() || y + rowsHeight < paintedRows.getY())
                continue;
            const uitrace::Part tally (uitrace::part::lanes);

            auto lane = t % 2 == 0 ? colour::bg2 : colour::bg2.interpolatedWith (colour::bg1, 0.5f);
            if (t == hover.track && drag == Drag::none)
                lane = lane.interpolatedWith (colour::textPrimary, 0.025f); // the track under the mouse
            g.setColour (lane);
            g.fillRect (grid.getX(), y, grid.getWidth(), trackHeight);
            g.setColour (colour::bg0);
            g.drawHorizontalLine (y + trackHeight - 1, (float) grid.getX(), (float) grid.getRight());

            if (const auto lanes = laneBlockHeight (t); lanes > 0) // round 177b: the track's automation lanes
            {
                g.setColour (colour::bg1.interpolatedWith (colour::bg2, 0.35f));
                g.fillRect (grid.getX(), y + trackHeight, grid.getWidth(), lanes);
                g.setColour (colour::bg0.withAlpha (0.6f));
                for (int i = 0; i < (int) song.playlistTracks[(std::size_t) t].lanes.size(); ++i)
                    g.drawHorizontalLine (laneRow (t, i).getBottom() - 1, (float) grid.getX(), (float) grid.getRight());
            }
        }

        const auto beatPixels = ppq * pixelsPerTick;

        // (only the bars of the repainted columns: a playhead strip is 16 px wide, the view can hold hundreds of bars,
        // and every line is a renderer call even where it is clipped away; round 174e)
        const auto paintedX = grid.getIntersection (g.getClipBounds());
        for (const auto& b : song.barsBetween (juce::jmax ((std::int64_t) std::max (0.0, scrollTicks), (std::int64_t) xToTick (paintedX.getX()) - bar),
                                               juce::jmin ((std::int64_t) xToTick (grid.getRight()), (std::int64_t) xToTick (paintedX.getRight())) + 1))
        {
            g.setColour (colour::gridBar);
            g.drawVerticalLine ((int) tickToX ((double) b.tick), (float) grid.getY(), (float) grid.getBottom());

            if (beatPixels < 8.0)
                continue; // zoomed far out: bar lines only

            g.setColour (colour::gridBeat);
            for (auto tick = b.tick + ppq; tick < b.tick + b.length; tick += ppq)
                g.drawVerticalLine ((int) tickToX ((double) tick), (float) grid.getY(), (float) grid.getBottom());
        }

        paintLanes (g, grid); // round 177b (nothing when lanes are off)

        // loop region
        if (song.hasLoop())
        {
            const auto lx0 = (float) tickToX ((double) song.loopStart), lx1 = (float) tickToX ((double) song.loopEnd);
            const auto tint = song.loopOff ? colour::textSecondary : colour::accent; // (round 190cg: grey while the loop is switched off)
            g.setColour (tint.withAlpha (0.06f));
            g.fillRect (juce::Rectangle<float> (lx0, (float) grid.getY(), lx1 - lx0, (float) grid.getHeight()));
            g.setColour (tint.withAlpha (0.45f));
            g.drawVerticalLine ((int) lx0, (float) grid.getY(), (float) grid.getBottom());
            g.drawVerticalLine ((int) lx1, (float) grid.getY(), (float) grid.getBottom());
        }

        // end of song
        if (! song.playlist.empty())
        {
            const auto endX = (float) tickToX ((double) song.songLength());
            g.setColour (colour::bg0.withAlpha (0.45f));
            g.fillRect (juce::Rectangle<float> (endX, (float) grid.getY(), (float) grid.getRight() - endX, (float) grid.getHeight()));
        }

        // Performance mode (round 165m): the zone before the Start marker, where clips are launched instead of played
        if (const auto zoneEnd = song.performanceZoneEnd(); zoneEnd > 0)
        {
            const auto zx0 = (float) tickToX (0.0), zx1 = (float) tickToX ((double) zoneEnd);
            if (zx1 > (float) grid.getX())
            {
                g.setColour (colour::info.withAlpha (0.05f));
                g.fillRect (juce::Rectangle<float> (zx0, (float) grid.getY(), zx1 - zx0, (float) grid.getHeight()));
                g.setColour (colour::info.withAlpha (0.55f));
                g.fillRect (juce::Rectangle<float> (zx1 - 1.0f, (float) grid.getY(), 2.0f, (float) grid.getHeight()));
            }
        }

        g.setFont (font (type::caption));

        // what each comp plays from its take lanes (round 177c); nothing is worked out without a comp track
        std::vector<comp::Used> compUsed;
        for (int t = 0; t < (int) song.playlistTracks.size(); ++t)
            if (song.playlistTracks[(std::size_t) t].compTrack)
                if (const auto group = comp::groupOf (song, t); group.valid() && group.comp == t)
                {
                    const auto used = comp::usedRanges (song, group);
                    compUsed.insert (compUsed.end(), used.begin(), used.end());
                }

        auto veilIfMuted = [&] (const Clip& clip, juce::Rectangle<float> area) {
            if (! clip.muted)
                return;
            std::vector<juce::Rectangle<float>> lit;
            if (! compUsed.empty())
            {
                const auto index = (int) (&clip - song.playlist.data());
                for (const auto& u : compUsed)
                    if (u.laneClip == index)
                    {
                        const auto x0 = juce::jmax (area.getX(), (float) tickToX ((double) u.from));
                        const auto x1 = juce::jmin (area.getRight(), (float) tickToX ((double) u.to));
                        if (x1 > x0)
                            lit.push_back ({ x0, area.getY(), x1 - x0, area.getHeight() });
                    }
            }
            paintMutedVeil (g, area, lit);
        };

        SmallClips smallClips;
        const auto physicalScale = g.getInternalContext().getPhysicalPixelScaleFactor();

        for (int i = 0; i < (int) song.playlist.size(); ++i)
        {
            const auto& c = song.playlist[(std::size_t) i];
            const auto x0 = (float) tickToX ((double) c.start);
            const auto x1 = (float) tickToX ((double) c.end());
            const auto y = (float) trackToY (c.track);
            const auto trackHeight = heightOf (c.track);

            if (x1 < grid.getX() || x0 > grid.getRight() || y > grid.getBottom() || y + trackHeight < grid.getY())
                continue;

            const auto r = juce::Rectangle<float> (x0, y + 1.0f, juce::jmax (3.0f, x1 - x0 - 1.0f), (float) trackHeight - 3.0f);

            if (! g.getClipBounds().toFloat().intersects (r.expanded (1.0f)))
                continue; // outside the area being repainted

            if (noteedit::isFoldedAway (song, c.track)) // a folded group: each member's clips as a thin bar (FL)
            {
                const auto barColour = c.kind == ClipKind::audio        ? colour::audioClip
                                       : c.kind == ClipKind::automation ? colour::automation
                                                                        : tame (patternColour (song, c.pattern));
                g.setColour (barColour.withAlpha (c.muted ? 0.35f : 0.9f));
                g.fillRect (juce::Rectangle<float> (x0, y + 2.0f, juce::jmax (2.0f, x1 - x0 - 1.0f), (float) foldedHeight - 4.0f));
                if (isSelected (i))
                {
                    g.setColour (colour::textPrimary);
                    g.drawRect (juce::Rectangle<float> (x0, y + 1.0f, juce::jmax (2.0f, x1 - x0 - 1.0f), (float) foldedHeight - 2.0f), 1.0f);
                }
                continue;
            }

            if (r.getWidth() < SmallClips::maxWidth && c.kind != ClipKind::automation && ! c.muted && ! isSelected (i))
            {
                addSmallClip (smallClips, physicalScale, c, r, c.kind == ClipKind::pattern && c.pattern == song.currentPattern);
                continue;
            }

            if (c.kind == ClipKind::automation)
            {
                paintAutomationClip (g, c, r);
                veilIfMuted (c, r);

                if (isSelected (i))
                    paintClipOutline (g, r, colour::automation, true, false);
                continue;
            }

            if (c.kind == ClipKind::audio)
            {
                paintAudioClip (g, c, r);
                veilIfMuted (c, r);

                if (isSelected (i))
                    paintClipOutline (g, r, colour::audioClip, true, false);
                continue;
            }

            const auto base = tame (patternColour (song, c.pattern));
            const auto current = c.pattern == song.currentPattern;
            const auto selected = isSelected (i);

            // body + name strip
            const auto clipBody = paintClipFrame (g, r, base, juce::String::fromUTF8 (song.patterns[(std::size_t) c.pattern].name.c_str()), ClipGlyph::pattern, c.muted);

            // note thumbnail: the pattern's notes as small rounded bars in a light tint of the clip colour,
            // spread over the body's height by pitch (a few keys of padding so single notes don't hug the edges)
            const auto& notes = song.patterns[(std::size_t) c.pattern].notes;

            if (! notes.empty() && r.getWidth() > 6 && clipBody.getHeight() >= 4.0f)
            {
                const uitrace::Part tally (uitrace::part::notes);
                int low = 127, high = 0;
                for (const auto& n : notes)
                {
                    low = juce::jmin (low, n.key);
                    high = juce::jmax (high, n.key);
                }

                const auto body = clipBody.reduced (1.0f, clipBody.getHeight() >= 12.0f ? 3.0f : 1.0f);
                const auto span = juce::jmax (6, high - low + 2); // a small pitch range stays centred
                const auto bottomKey = (low + high) / 2.0f - span / 2.0f;
                const auto rows = (float) span + 1.0f;
                const auto noteHeight = juce::jlimit (1.5f, 2.5f, body.getHeight() / rows);
                g.setColour (clipContentColour (base));

                juce::Graphics::ScopedSaveState clip (g);
                g.reduceClipRegion (body.toNearestInt());
                const auto visible = g.getClipBounds().toFloat();
                // small notes as plain rectangles (a rounded one is a path each on the GPU renderer; at 2 px the
                // rounding does not show), notes outside the repainted area skipped
                const auto rounded = noteHeight > 2.0f && r.getWidth() > 60.0f;

                // what plays: a clip stretched past its pattern loops it with the LYNX option (issue #25, default on),
                // else the pattern once and empty after it, as FL plays and shows it (round 168k [EXEC])
                const auto passes = clipPasses (c, song.patterns[(std::size_t) c.pattern].effectiveLength (song.beatsPerBar));
                for (auto pass = passes.first; pass <= passes.last; ++pass)
                {
                    const auto origin = c.start - c.offset + pass * passes.length;
                    if (passes.loops && (tickToX ((double) origin) > visible.getRight() || tickToX ((double) (origin + passes.length)) < visible.getX()))
                        continue;
                    for (const auto& n : notes)
                    {
                        const auto at = origin + n.start;
                        if ((passes.loops && n.start >= passes.length) || at + n.length <= c.start || at >= c.end())
                            continue;
                        const auto nx = (float) tickToX ((double) at);
                        const auto nw = juce::jmax (rounded ? 2.0f : 1.0f, (float) (n.length * pixelsPerTick) - 1.0f);
                        if (nx > visible.getRight() || nx + nw < visible.getX())
                            continue;
                        const auto ny = body.getBottom() - ((float) n.key - bottomKey + 0.5f) / rows * body.getHeight() - noteHeight * 0.5f;
                        if (rounded)
                            g.fillRoundedRectangle (nx, ny, nw, noteHeight, juce::jmin (1.0f, noteHeight * 0.5f));
                        else
                            g.fillRect (nx, ny, nw, noteHeight);
                    }
                }
            }

            veilIfMuted (c, r);
            paintClipOutline (g, r, base, selected, current);
        }

        {
            const uitrace::Part tally (uitrace::part::batch);
            smallClips.fill (g);
        }

        if (song.performanceZoneEnd() > 0)
            paintPerformance (g, grid);

        if (engine.isLiveRecording())
            paintLiveRecording (g);

        // the clip under the mouse: outline, the grip of the edge it is on, its menu button
        if (hover.clip >= 0 && hover.clip < (int) song.playlist.size() && drag == Drag::none)
        {
            const auto r = clipBounds (hover.clip).toFloat();
            g.setColour (colour::textPrimary.withAlpha (isSelected (hover.clip) ? 0.0f : 0.55f));
            g.drawRoundedRectangle (r.reduced (0.5f), radius::sm, 1.2f);

            if (hover.target == gesture::Target::leftEdge || hover.target == gesture::Target::rightEdge)
            {
                const auto x = hover.target == gesture::Target::rightEdge ? r.getRight() - 4.0f : r.getX() + 1.0f;
                g.setColour (colour::textPrimary);
                g.fillRoundedRectangle (x, r.getY() + clipTitleHeight + 3.0f, 3.0f, juce::jmax (4.0f, r.getHeight() - clipTitleHeight - 6.0f), 1.5f);
            }

            if (const auto button = clipMenuButton (hover.clip); ! button.isEmpty())
            {
                const auto b = button.toFloat();
                g.setColour (hover.target == gesture::Target::clipMenu ? colour::bg0.withAlpha (0.75f) : colour::bg0.withAlpha (0.45f));
                g.fillRoundedRectangle (b, 2.0f);
                icons::draw (g, Icon::chevronDown, b.reduced (1.0f), colour::textPrimary);
            }
        }

        // FL 2026's gain handle (round 166e): the hovered audio clip's, and the one being dragged
        if (const auto gainClip = drag == Drag::clipGain ? dragClip : drag == Drag::none ? hover.clip : -1; gainClip >= 0 && gainClip < (int) song.playlist.size())
            if (const auto handle = gainHandle (gainClip); ! handle.isEmpty())
            {
                const auto active = drag == Drag::clipGain || hover.target == gesture::Target::gainHandle;
                juce::Path half;
                half.addPieSegment (handle.getX(), handle.getY(), handle.getWidth(), handle.getWidth(), -juce::MathConstants<float>::halfPi,
                                    juce::MathConstants<float>::halfPi, 0.0f);
                g.setColour (active ? colour::textPrimary : colour::textPrimary.withAlpha (0.7f));
                g.fillPath (half);
                g.setColour (colour::bg0.withAlpha (0.8f));
                g.strokePath (half, juce::PathStrokeType (1.0f));
            }

        if (! rubberBand.isEmpty())
        {
            g.setColour (colour::selectionFill);
            g.fillRect (rubberBand);
            g.setColour (colour::selection);
            g.drawRect (rubberBand);
        }

        if (drag == Drag::knife)
        {
            juce::Path cut;
            cut.startNewSubPath (knifeFrom.toFloat());
            cut.lineTo (knifeTo.toFloat());
            juce::Path dashed;
            const float dash[] = { 5.0f, 3.0f };
            juce::PathStrokeType (1.5f).createDashedStroke (dashed, cut, dash, 2);
            g.setColour (colour::textPrimary);
            g.fillPath (dashed);
        }

        if (playheadTick >= 0.0 && ! cachePainting)
        {
            g.setColour (colour::playhead);
            g.fillRect (juce::Rectangle<float> ((float) tickToX (playheadTick) - 0.5f, (float) grid.getY(), 1.5f, (float) grid.getHeight()));
        }
    }

    // --- ruler (drawn in its own coordinates, below the toolbar) -----------------
    {
        const uitrace::Scope traceRuler ("Playlist ruler", false);
        juce::Graphics::ScopedSaveState state (g);
        g.setOrigin (0, rulerTop);
        const auto ruler = juce::Rectangle<int> (headerWidth, 0, grid.getWidth(), timelineHeight);
        g.setColour (colour::bg2);
        g.fillRect (ruler);
        g.setColour (colour::border);
        g.fillRect (ruler.withTop (ruler.getBottom() - 1));
        g.reduceClipRegion (ruler);
        g.setFont (font (type::small));

        if (song.hasLoop()) // the time selection
        {
            const auto lx0 = (float) tickToX ((double) song.loopStart), lx1 = (float) tickToX ((double) song.loopEnd);
            const auto tint = song.loopOff ? colour::textSecondary : colour::accent;
            g.setColour (tint.withAlpha (0.16f));
            g.fillRect (juce::Rectangle<float> (lx0, 0.0f, lx1 - lx0, (float) timelineHeight));
            g.setColour (tint.withAlpha (0.75f));
            g.fillRect (juce::Rectangle<float> (lx0, 0.0f, lx1 - lx0, 4.0f));
        }

        const auto barPixels = bar * pixelsPerTick;
        const auto every = barPixels < 30.0 ? (barPixels < 10.0 ? 16 : 4) : 1; // numbers spaced further apart when zoomed out

        const auto rulerClip = g.getClipBounds(); // (the numbers reach 40 px right of their bar line)
        for (const auto& b : song.barsBetween (juce::jmax ((std::int64_t) std::max (0.0, scrollTicks), (std::int64_t) xToTick (rulerClip.getX() - 44) - bar),
                                               juce::jmin ((std::int64_t) xToTick (grid.getRight()), (std::int64_t) xToTick (rulerClip.getRight())) + 1))
        {
            if ((b.number - 1) % every != 0)
                continue;

            const auto x = (int) tickToX ((double) b.tick);
            g.setColour (colour::textDisabled);
            g.drawVerticalLine (x, 12.0f, (float) timelineHeight);
            g.setColour (colour::textSecondary);
            textcache::drawText (g, juce::String (b.number), x + 3, 2, 40, timelineHeight - 4, juce::Justification::centredLeft);
        }

        // markers: small flag with the name; time signature changes show the fraction
        g.setFont (font (type::caption, true));

        for (const auto& m : song.markers())
        {
            const auto x = (float) tickToX ((double) m.tick);

            if (x < (float) ruler.getX() - 200.0f || x > (float) ruler.getRight())
                continue;

            const auto label = markerLabel (m);
            const auto width = (float) juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), label) + 8.0f;
            const auto tag = juce::Rectangle<float> (x, (float) timelineHeight - 11.0f, width, 11.0f);
            // recording markers (Start / Stop recording) in the accent colour, as the record button
            const auto punch = m.action == Marker::punchIn || m.action == Marker::punchOut;
            const auto acts = ! m.isSignature() && m.action != 0 && ! punch;
            g.setColour (m.isSignature() ? colour::info : punch ? colour::accent : acts ? colour::automation : colour::warning);
            g.fillRoundedRectangle (tag, 2.0f);
            g.drawVerticalLine ((int) x, 0.0f, (float) timelineHeight);
            g.setColour (colour::bg0);
            textcache::drawText (g, label, tag.reduced (3.0f, 0.0f), juce::Justification::centredLeft, false);
        }

        if (playheadTick >= 0.0 && ! cachePainting)
        {
            const auto x = (float) tickToX (playheadTick);
            juce::Path tri;
            tri.addTriangle (x - 6, 0, x + 6, 0, x, 9);
            g.setColour (colour::playhead);
            g.fillPath (tri);
        }
    }

    // --- track headers -----------------------------------------------------
    // FL's layout: the name on the left, a group header's fold arrow and the mute LED on the right; grouped
    // tracks sit indented on a block in their header's colour; members of a folded group are thin rows
    {
        const uitrace::Scope traceHeaders ("Playlist headers", false);
        updateMeterColumn(); // round 190w (a --snapshot paints before any frame step)
        const auto headers = juce::Rectangle<int> (0, tracksTop(), headerWidth, grid.getHeight());
        juce::Graphics::ScopedSaveState state (g);
        if (g.reduceClipRegion (headers)) // (not repainted, e.g. the playhead's strip: nothing to draw; round 174e)
        {
            g.setColour (colour::bg2);
            g.fillRect (headers);
            g.setFont (font (type::small));

            auto colourOf = [&song] (int t) { return t >= 0 && t < (int) song.playlistTracks.size() ? song.playlistTracks[(std::size_t) t].colour : 0u; };

            // group blocks first (behind the headers): from the header's top to the last member's bottom
            for (int t = 0; t < tracks; ++t)
            {
                if (! noteedit::isGroupHeader (song, t))
                    continue;
                const auto members = noteedit::groupMembers (song, t);
                const auto last = members.empty() ? t : members.back();
                const auto top = trackToY (t), bottom = trackToY (last) + heightOf (last);
                if (top > headers.getBottom() || bottom < headers.getY())
                    continue;
                auto block = colour::bg3;
                if (const auto c = colourOf (t); c != 0)
                    block = block.interpolatedWith (tame (juce::Colour (c)), 0.28f);
                g.setColour (block);
                g.fillRoundedRectangle (juce::Rectangle<float> (1.0f, (float) top + 1.0f, (float) headerWidth - 2.0f, (float) (bottom - top) - 2.0f), radius::md);
            }

            for (int t = 0; t < tracks; ++t)
            {
                const auto y = trackToY (t);
                const auto trackHeight = heightOf (t);

                if (y > headers.getBottom() || y + trackHeight < headers.getY())
                    continue;

                if (noteedit::isFoldedAway (song, t))
                    continue; // the group block shows through: a thin row, as FL draws a folded group

                const auto muted = song.isTrackMuted (t);
                const auto custom = t < (int) song.playlistTracks.size() && ! song.playlistTracks[(std::size_t) t].name.empty();
                const auto hovered = hover.header && hover.track == t;
                const auto member = noteedit::groupHeaderOf (song, t) >= 0 && ! noteedit::isGroupHeader (song, t);
                const auto groupHead = noteedit::isGroupHeader (song, t);
                const auto left = member ? (float) groupIndent : 2.0f;
                auto plate = muted ? colour::bg2 : colour::bg3;
                const auto trackColour = colourOf (t);
                if (trackColour != 0) // the track's colour (FL projects colour their track headers by group)
                    plate = plate.interpolatedWith (tame (juce::Colour (trackColour)), muted ? 0.22f : (groupHead ? 0.62f : 0.5f));
                if (hovered)
                    plate = plate.interpolatedWith (colour::bg5, 0.6f);
                const auto plateArea = juce::Rectangle<float> (left, (float) y + 1.0f, (float) (headerWidth - meterColumn) - 2.0f - left, (float) trackHeight - 3.0f);
                g.setColour (plate);
                g.fillRoundedRectangle (plateArea, radius::sm);

                // round 190w: FL's track header level (View › Show levels on audio / instrument tracks): a stereo peak
                // meter in a column right of the plate, the mixer track's level with its hold line, as FL 2026 draws it
                if (const auto insert = meteredInsertOf (t); insert >= 0 && meterColumn > 0)
                {
                    const auto well = juce::Rectangle<int> (headerWidth - meterColumn, y + 2, meterColumn - 2, trackHeight - 5);
                    g.setColour (colour::bg1);
                    g.fillRect (well);
                    const auto found = headerLevels.find (insert);
                    for (int side = 0; side < 2; ++side)
                    {
                        const auto bar = juce::Rectangle<int> (well.getX() + 1 + side * (well.getWidth() / 2), well.getY() + 1,
                                                               well.getWidth() / 2 - 1, well.getHeight() - 2);
                        if (found == headerLevels.end())
                            continue;
                        const auto& m = found->second[(std::size_t) side];
                        auto position = [] (float gain) { // the Mixer's scale: -60..+6 dB
                            return juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (gain, -60.0f) + 60.0f) / 66.0f);
                        };
                        const auto h = (int) (position (m.level) * (float) bar.getHeight());
                        g.setColour (colour::meterLow); // (FL's header meter stays green at every level)
                        g.fillRect (bar.withTop (bar.getBottom() - h));
                        if (m.peak > 0.001f) // the hold line
                        {
                            const auto py = bar.getBottom() - (int) (position (m.peak) * (float) bar.getHeight());
                            g.setColour (colour::meterLow);
                            g.fillRect (bar.getX(), juce::jmax (bar.getY(), py - 1), bar.getWidth(), 1);
                        }
                    }
                }

                if (isTrackSelected (t)) // the selected tracks (FL marks them next to the LED)
                {
                    g.setColour (colour::accent);
                    g.fillRoundedRectangle (plateArea.withWidth (3.0f).reduced (0.0f, 3.0f), 1.5f);
                }

                const auto led = trackLed (t).toFloat();
                auto textRight = led.getX() - 2.0f;
                if (simpleHeaders)
                {
                    // the Simple headers (round 184a): M and S buttons on the track's insert, a colour bar, and on tall
                    // rows the insert's volume, pan and effects
                    if (trackColour != 0)
                    {
                        g.setColour (juce::Colour (trackColour));
                        g.fillRoundedRectangle (plateArea.withWidth (4.0f).reduced (0.0f, 2.0f).translated (isTrackSelected (t) ? 4.0f : 0.0f, 0.0f), 1.5f);
                    }
                    const auto insert = simpleInsert (t);
                    const auto* m = insert > 0 ? &song.mixer[(std::size_t) insert] : nullptr;
                    const auto button = [&g] (juce::Rectangle<float> r, const char* text, bool on, juce::Colour onColour, bool lit) {
                        g.setColour (on ? onColour : (lit ? colour::bg6 : colour::bg4));
                        g.fillRoundedRectangle (r, radius::sm);
                        g.setColour (on ? colour::bg0 : (lit ? colour::textPrimary : colour::textSecondary));
                        g.setFont (font (type::caption, true));
                        g.drawText (text, r, juce::Justification::centred, false);
                    };
                    button (led, "M", m != nullptr ? m->muted : muted, colour::warning, hovered && hover.led);
                    if (const auto solo = trackSolo (t).toFloat(); ! solo.isEmpty() && m != nullptr)
                    {
                        button (solo, "S", m->solo, colour::info, hovered && hover.solo);
                        textRight = solo.getX() - 2.0f;
                    }
                    if (const auto bar = trackVolume (t).toFloat(); ! bar.isEmpty() && m != nullptr)
                    {
                        // volume: a bar filled up to the fader position, a tick at 0 dB
                        g.setColour (colour::bg0);
                        g.fillRoundedRectangle (bar.withSizeKeepingCentre (bar.getWidth(), 6.0f), 3.0f);
                        g.setColour ((hovered && hover.volume) || (levelDrag == 1 && levelInsert == insert) ? colour::accentHover : colour::accent);
                        g.fillRoundedRectangle (bar.withSizeKeepingCentre (bar.getWidth(), 6.0f).withWidth (juce::jmax (6.0f, bar.getWidth() * m->volume)), 3.0f);
                        g.setColour (colour::textSecondary.withAlpha (0.6f));
                        g.fillRect (bar.getX() + bar.getWidth() * unityFader - 0.5f, bar.getY(), 1.0f, bar.getHeight());
                        // pan: a small knob, its arc from the top
                        const auto pan = trackPan (t).toFloat();
                        g.setColour (colour::bg4);
                        g.fillEllipse (pan.reduced (1.0f));
                        juce::Path arc;
                        const auto angle = m->pan * 2.356f;
                        arc.addCentredArc (pan.getCentreX(), pan.getCentreY(), 7.0f, 7.0f, 0.0f, juce::jmin (0.0f, angle), juce::jmax (0.0f, angle), true);
                        g.setColour ((hovered && hover.pan) || (levelDrag == 2 && levelInsert == insert) ? colour::accentHover : colour::info);
                        g.strokePath (arc, juce::PathStrokeType (2.0f));
                        g.setColour (colour::textPrimary);
                        g.drawLine (pan.getCentreX(), pan.getCentreY(), pan.getCentreX() + 6.0f * std::sin (angle), pan.getCentreY() - 6.0f * std::cos (angle), 1.4f);
                        // "+FX", with the insert's effect count
                        const auto fx = trackFx (t).toFloat();
                        const auto count = (int) m->effects.size();
                        g.setColour (hovered && hover.fx ? colour::bg6 : colour::bg4);
                        g.fillRoundedRectangle (fx, radius::sm);
                        g.setColour (count > 0 ? colour::accent : colour::textSecondary);
                        g.setFont (font (type::caption, true));
                        g.drawText (count > 0 ? "FX " + juce::String (count) : juce::String ("+ FX"), fx, juce::Justification::centred, false);
                    }
                }
                else
                {
                    g.setColour (muteLed (muted));
                    g.fillEllipse (led.withSizeKeepingCentre (8.0f, 8.0f));
                    if (hovered && hover.led) // the LED is a button: ring it
                    {
                        g.setColour (colour::textPrimary.withAlpha (0.7f));
                        g.drawEllipse (led.withSizeKeepingCentre (12.0f, 12.0f), 1.0f);
                    }
                }

                // a take lane's audition button (round 177c): a small speaker, lit while this lane plays instead of the comp
                if (const auto box = trackAudition (t); ! box.isEmpty())
                {
                    const auto on = song.auditionLane == t;
                    const auto b = box.toFloat().withSizeKeepingCentre (10.0f, 10.0f);
                    g.setColour (on ? colour::accent : (hovered && hover.audition ? colour::textPrimary : colour::textSecondary));
                    juce::Path speaker;
                    const auto cy = b.getCentreY();
                    speaker.startNewSubPath (b.getX(), cy - 1.8f);
                    speaker.lineTo (b.getX() + 2.2f, cy - 1.8f);
                    speaker.lineTo (b.getX() + 5.0f, cy - 4.2f);
                    speaker.lineTo (b.getX() + 5.0f, cy + 4.2f);
                    speaker.lineTo (b.getX() + 2.2f, cy + 1.8f);
                    speaker.lineTo (b.getX(), cy + 1.8f);
                    speaker.closeSubPath();
                    g.fillPath (speaker);
                    juce::Path wave;
                    wave.addCentredArc (b.getX() + 5.0f, cy, 3.6f, 3.6f, 0.0f, 0.5f, juce::MathConstants<float>::pi - 0.5f, true);
                    g.strokePath (wave, juce::PathStrokeType (1.1f));
                }

                // an audio track's record arm (and, on tall rows, input) button: red while its mixer track is armed (FL 2026)
                if (const auto armBox = trackArm (t); ! armBox.isEmpty())
                {
                    const auto insert = trackmodes::armableInsert (song, t);
                    const auto armed = engine.isInsertArmed (insert);
                    const auto b = armBox.toFloat();
                    const auto lit = hovered && hover.arm;
                    if (armed)
                    {
                        g.setColour (colour::record);
                        g.fillEllipse (b.withSizeKeepingCentre (10.0f, 10.0f));
                    }
                    else
                    {
                        g.setColour ((lit ? colour::textPrimary : colour::textSecondary).withAlpha (muted ? 0.5f : 1.0f));
                        g.fillEllipse (b.withSizeKeepingCentre (4.0f, 4.0f));
                    }
                    g.setColour (armed ? colour::record.brighter (lit ? 0.4f : 0.0f) : (lit ? colour::textPrimary : colour::textSecondary.withAlpha (0.7f)));
                    g.drawEllipse (b.withSizeKeepingCentre (10.0f, 10.0f), 1.2f);
                    textRight = b.getX() - 2.0f;

                    if (const auto inBox = trackInput (t); ! inBox.isEmpty())
                    {
                        // "Audio input source": an arrow into a bar, lit when the mixer track has an input
                        const auto r = inBox.toFloat().reduced (1.5f);
                        const auto hasInput = song.mixer[(std::size_t) insert].inputChannel >= 0;
                        g.setColour (hovered && hover.input ? colour::textPrimary : (hasInput ? colour::accent : colour::textSecondary));
                        juce::Path in;
                        const auto cy = r.getCentreY();
                        in.startNewSubPath (r.getX(), cy);
                        in.lineTo (r.getRight() - 3.5f, cy);
                        in.startNewSubPath (r.getRight() - 6.5f, cy - 3.0f);
                        in.lineTo (r.getRight() - 3.5f, cy);
                        in.lineTo (r.getRight() - 6.5f, cy + 3.0f);
                        g.strokePath (in, juce::PathStrokeType (1.2f));
                        g.fillRect (juce::Rectangle<float> (r.getRight() - 1.5f, r.getY(), 1.3f, r.getHeight()));
                        textRight = r.getX() - 2.0f;
                    }
                }

                if (const auto arrow = trackArrow (t); ! arrow.isEmpty())
                {
                    const auto collapsed = t < (int) song.playlistTracks.size() && song.playlistTracks[(std::size_t) t].collapsed;
                    const auto a = arrow.toFloat().withSizeKeepingCentre (8.0f, 5.0f);
                    juce::Path tri;
                    if (collapsed) // folded: points down (FL), open: up
                        tri.addTriangle (a.getX(), a.getY(), a.getRight(), a.getY(), a.getCentreX(), a.getBottom());
                    else
                        tri.addTriangle (a.getX(), a.getBottom(), a.getRight(), a.getBottom(), a.getCentreX(), a.getY());
                    g.setColour (hovered && hover.arrow ? colour::textPrimary : colour::textSecondary);
                    g.fillPath (tri);
                    textRight = juce::jmin (textRight, (float) arrow.getX());
                }
                if (const auto box = trackAudition (t); ! box.isEmpty())
                    textRight = juce::jmin (textRight, (float) box.getX());

                // the track's icon before its name, as FL's track header shows it (round 168h)
                auto textLeft = (int) left + (simpleHeaders ? 12 : 6);
                // the Simple headers: the name in the first row, beside the buttons
                const auto nameHeight = simpleHeaders && ! trackVolume (t).isEmpty() ? juce::jmin (trackHeight, (int) led.getBottom() - y + 4) : trackHeight;

                // round 177b: the lanes' fold arrow (points down while they show) and, on hover, "+A" (add a lane)
                if (const auto laneArrow = trackLaneArrow (t); ! laneArrow.isEmpty())
                {
                    const auto folded = song.playlistTracks[(std::size_t) t].lanesFolded;
                    const auto a = laneArrow.toFloat().withSizeKeepingCentre (folded ? 5.0f : 8.0f, folded ? 8.0f : 5.0f);
                    juce::Path tri;
                    if (folded) // points right
                        tri.addTriangle (a.getX(), a.getY(), a.getX(), a.getBottom(), a.getRight(), a.getCentreY());
                    else
                        tri.addTriangle (a.getX(), a.getY(), a.getRight(), a.getY(), a.getCentreX(), a.getBottom());
                    g.setColour (hovered && laneArrow.contains (lastMouse) ? colour::textPrimary : colour::automation.brighter (0.2f));
                    g.fillPath (tri);
                    textLeft = laneArrow.getRight() + 3;
                }
                if (const auto plus = trackAddLane (t); hovered && ! plus.isEmpty())
                {
                    const auto lit = plus.contains (lastMouse);
                    g.setColour (lit ? colour::automation.withAlpha (0.35f) : colour::bg2.withAlpha (0.8f));
                    g.fillRoundedRectangle (plus.toFloat(), radius::sm);
                    g.setColour (lit ? colour::textPrimary : colour::automation.brighter (0.3f));
                    g.setFont (font (type::caption, true));
                    textcache::drawText (g, "+A", plus, juce::Justification::centred);
                    textRight = juce::jmin (textRight, (float) plus.getX());
                }
                if (const auto icon = trackicons::iconFor (t < (int) song.playlistTracks.size() ? song.playlistTracks[(std::size_t) t].icon : 0);
                    icon != trackicons::Icon::none && trackHeight >= 14)
                {
                    const auto size = (float) juce::jmin (14, trackHeight - 4);
                    ui::drawTrackIcon (g, icon, juce::Rectangle<float> ((float) textLeft, (float) y + ((float) nameHeight - size) * 0.5f, size, size),
                                       muted ? colour::textDisabled : colour::textSecondary.brighter (0.3f));
                    textLeft += (int) size + 4;
                }
                g.setColour (muted ? colour::textDisabled : (custom ? colour::textPrimary : colour::textSecondary));
                g.setFont (font (simpleHeaders ? type::body : type::small, groupHead || (simpleHeaders && custom)));
                textcache::drawFittedText (g, custom ? juce::String::fromUTF8 (song.playlistTracks[(std::size_t) t].name.c_str())
                                         : tr ("Track {0}", { juce::String (t + 1) }),
                                  textLeft, y, juce::jmax (8, (int) textRight - textLeft - 2), nameHeight, juce::Justification::centredLeft,
                                  nameHeight >= 30 ? 2 : 1, 0.8f);

                // the divider under the header: lit while hovered or dragged (drag resizes the track)
                if (hover.divider == t || (drag == Drag::trackResize && (resizeTrack == t || resizeAll)))
                {
                    g.setColour (colour::accent);
                    g.fillRoundedRectangle (juce::Rectangle<float> (6.0f, (float) (y + trackHeight) - 2.0f, (float) headerWidth - 12.0f, 3.0f), 1.5f);
                }
            }

            paintLaneHeaders (g, headers); // round 177b (nothing when lanes are off)
        }
    }

    // the Simple workflow's drop lane (round 184a): the first empty row invites files
    if (const auto laneTrack = dropLaneTrack(); laneTrack >= 0)
    {
        juce::Graphics::ScopedSaveState state (g);
        const auto y = trackToY (laneTrack), h = heightOf (laneTrack);
        if (g.reduceClipRegion (grid) && y < grid.getBottom() && y + h > grid.getY())
        {
            const auto lane = juce::Rectangle<float> ((float) grid.getX() + 6.0f, (float) y + 4.0f, (float) grid.getWidth() - 12.0f, (float) h - 8.0f);
            juce::Path outline;
            outline.addRoundedRectangle (lane, radius::lg);
            juce::Path dashed;
            const float dashes[] { 6.0f, 5.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dashes, 2);
            g.setColour (colour::borderStrong);
            g.fillPath (dashed);
            g.setColour (colour::textSecondary);
            g.setFont (font (type::body));
            g.drawText (tr ("Drop a loop, audio or MIDI file here"), lane.reduced (12.0f, 0.0f), juce::Justification::centredLeft, true);
        }
    }

    // toolbar band, and the corner left of the ruler
    g.setColour (colour::bg3);
    g.fillRect (0, 0, getWidth(), toolbarHeight);
    g.setColour (colour::bg2);
    g.fillRect (0, toolbarHeight, headerWidth, headerHeight - toolbarHeight);
    g.setColour (colour::border);
    g.fillRect (0, toolbarHeight - 1, getWidth(), 1);
    g.fillRect (0, headerHeight - 1, headerWidth, 1);
    g.fillRect (headerWidth - 1, toolbarHeight, 1, getHeight() - toolbarHeight);

    // the vertical zoom handle above the scroll bar (FL's track height control): a grip of three lines
    {
        const auto r = trackZoomHandle().toFloat().reduced (1.0f, 2.0f);
        const auto active = trackZoomHover || drag == Drag::trackZoom;
        theme::fillRoundRect (g, r, theme::radius::sm, active ? colour::bg4 : colour::bg2);
        g.setColour (active ? colour::accent : (std::abs (trackZoom - 1.0) > 1.0e-3 ? colour::textSecondary : colour::textDisabled));
        const auto cx = r.getCentreX(), cy = r.getCentreY();
        for (int i = -1; i <= 1; ++i)
            g.fillRect (juce::Rectangle<float> (cx - 3.0f, cy + (float) i * 4.0f - 0.5f, 6.0f, 1.2f));
        g.drawLine (cx, r.getY() + 2.0f, cx, cy - 6.0f, 1.0f);
        g.drawLine (cx, cy + 6.0f, cx, r.getBottom() - 2.0f, 1.0f);
    }
    uitrace::flushParts ("Playlist", trace.elapsedMs());
}

void PlaylistComponent::paintOverview (juce::Graphics& g, juce::Rectangle<float> area, double total)
{
    // every clip of the arrangement in miniature (its colour, its track's row), bar lines, the song's end
    using namespace theme;
    const auto& song = engine.getSong();
    const auto scale = area.getWidth() / juce::jmax (1.0, total);
    const auto bar = (double) ppq * juce::jmax (1, song.beatsPerBar);

    g.setColour (colour::bg2);
    g.fillRect (area.withWidth (juce::jmin (area.getWidth(), (float) (song.songLength() * scale))));

    const auto every = bar * scale < 3.0 ? 16 : bar * scale < 6.0 ? 4 : 1;
    g.setColour (colour::gridBar.withAlpha (0.5f));
    for (int b = every; b * bar < total; b += every)
        g.fillRect ((float) (area.getX() + b * bar * scale), area.getY(), 1.0f, area.getHeight());

    int rows = 1;
    for (const auto& c : song.playlist)
        rows = juce::jmax (rows, c.track + 1);

    const auto inner = area.reduced (0.0f, 1.0f);
    const auto rowHeight = inner.getHeight() / (float) rows;

    for (const auto& c : song.playlist)
    {
        auto base = colour::automation;
        if (c.kind == ClipKind::pattern && c.pattern >= 0 && c.pattern < (int) song.patterns.size())
            base = tame (patternColour (song, c.pattern));
        else if (c.kind == ClipKind::audio && c.channel >= 0 && c.channel < (int) song.channels.size())
            base = tame (audioClipColour (song.channels[(std::size_t) c.channel]));

        const auto y = inner.getY() + rowHeight * (float) c.track;
        g.setColour (c.muted ? base.withAlpha (0.35f) : base);
        g.fillRect ((float) (area.getX() + c.start * scale), y, juce::jmax (1.0f, (float) (c.length * scale) - 0.5f),
                    juce::jmax (1.0f, rowHeight - (rowHeight >= 3.0f ? 0.5f : 0.0f)));
    }
}

// The playhead over the cached image (round 174e): the line in the grid and the triangle on the ruler, as paint()
// draws them without the cache
void PlaylistComponent::paintPlayhead (juce::Graphics& g)
{
    if (playheadTick < 0.0)
        return;
    using namespace theme;
    const auto grid = gridArea();
    const auto x = (float) tickToX (playheadTick);
    {
        juce::Graphics::ScopedSaveState state (g);
        if (g.reduceClipRegion (grid))
        {
            g.setColour (colour::playhead);
            g.fillRect (juce::Rectangle<float> (x - 0.5f, (float) grid.getY(), 1.5f, (float) grid.getHeight()));
        }
    }
    juce::Graphics::ScopedSaveState state (g);
    if (g.reduceClipRegion (juce::Rectangle<int> (headerWidth, rulerTop, grid.getWidth(), timelineHeight)))
    {
        juce::Path tri;
        tri.addTriangle (x - 6, (float) rulerTop, x + 6, (float) rulerTop, x, (float) rulerTop + 9.0f);
        g.setColour (colour::playhead);
        g.fillPath (tri);
    }
}

// Live's select + Enter (round 177c, comp::swipeStyle): the time selection of a take lane goes to its comp.
void PlaylistComponent::swipeTimeSelectionToComp (int lane)
{
    const auto& song = engine.getSong();
    const auto g = comp::groupOf (song, lane);
    if (! comp::selectionSwipes (comp::swipeStyle) || ! g.isLane (lane) || ! comp::isComp (song, g.comp) || ! exportopts::hasSelection (song))
        return;
    setSelection ({});
    engine.editSong ([lane] (Song& s) { comp::swipeSelection (s, lane); });
    sortClips();
}

void PlaylistComponent::paintOverChildren (juce::Graphics& g)
{
    if (drag == Drag::swipe && swipeLane >= 0 && swipeEnd != swipeAnchor)
    {
        const auto x0 = (float) tickToX ((double) juce::jmin (swipeAnchor, swipeEnd));
        const auto x1 = (float) tickToX ((double) juce::jmax (swipeAnchor, swipeEnd));
        const auto range = juce::Rectangle<float> (x0, (float) trackToY (swipeLane), x1 - x0, (float) heightOf (swipeLane));
        const juce::Graphics::ScopedSaveState keep (g);
        g.reduceClipRegion (gridArea());
        g.setColour (theme::colour::accent.withAlpha (0.28f));
        g.fillRect (range);
        g.setColour (theme::colour::accent);
        g.drawRect (range, 1.0f);
    }

    const auto& song = engine.getSong();
    if (song.performanceMode)
    {
        // Performance mode: a tag in the grid's top-right corner (left of the Ripple tag), and each playing track's progress on its header
        using namespace theme;
        const auto grid = gridArea();
        const auto tag = juce::Rectangle<float> ((float) grid.getRight() - 176.0f, (float) grid.getY() + 4.0f, 100.0f, 18.0f);
        g.setColour (colour::bg0.withAlpha (0.8f));
        g.fillRoundedRectangle (tag, radius::sm);
        g.setColour (colour::info);
        g.drawRoundedRectangle (tag.reduced (0.5f), radius::sm, 1.0f);
        g.setFont (font (type::caption, true));
        textcache::drawText (g, tr ("Performance"), tag.toNearestInt(), juce::Justification::centred);

        const auto& live = engine.performanceState();
        if (showTrackProgress && live.anyPlaying.load (std::memory_order_relaxed))
        {
            juce::Graphics::ScopedSaveState state (g);
            g.reduceClipRegion (0, grid.getY(), headerWidth, grid.getHeight());
            for (int t = 0; t < juce::jmin (numTracks(), perf::maxTracks); ++t)
            {
                const auto& view = live.tracks[(std::size_t) t];
                if (view.playing.load (std::memory_order_relaxed) < 0)
                    continue;
                const auto y = trackToY (t), h = heightOf (t);
                if (y > grid.getBottom() || y + h < grid.getY())
                    continue;
                const auto p = juce::jlimit (0.0f, 1.0f, view.progress.load (std::memory_order_relaxed));
                const auto bar = juce::Rectangle<float> (4.0f, (float) (y + h) - 5.0f, (float) headerWidth - 8.0f, 3.0f);
                g.setColour (colour::bg0);
                g.fillRect (bar);
                g.setColour (colour::ledOn);
                g.fillRect (bar.withWidth (bar.getWidth() * p));
            }
        }
    }

    if (! ripple)
        return;

    // ripple editing is on: a small tag in the grid's top-right corner
    const auto grid = gridArea();
    const auto tag = juce::Rectangle<float> ((float) grid.getRight() - 70.0f, (float) grid.getY() + 4.0f, 64.0f, 18.0f);
    g.setColour (theme::colour::accentMuted);
    g.fillRoundedRectangle (tag, theme::radius::sm);
    g.setColour (theme::colour::accent);
    g.drawRoundedRectangle (tag.reduced (0.5f), theme::radius::sm, 1.0f);
    g.setFont (theme::font (theme::type::caption, true));
    textcache::drawText (g, tr ("Ripple"), tag.toNearestInt(), juce::Justification::centred);
}

void PlaylistComponent::addSmallClip (SmallClips& batch, float physicalScale, const Clip& c, juce::Rectangle<float> r, bool current)
{
    const auto& song = engine.getSong();
    juce::Colour base;
    if (c.kind == ClipKind::audio)
    {
        if (c.channel < 0 || c.channel >= (int) song.channels.size())
            return;
        base = audioClipColour (song.channels[(std::size_t) c.channel]);
    }
    else
        base = theme::tame (patternColour (song, c.pattern));

    const auto title = r.withHeight (clipHeaderHeight (r));
    const auto body = r.withTrimmedTop (title.getHeight());
    batch.bodies[clipBodyColour (base).getARGB()].addWithoutMerging (body);
    if (title.getHeight() >= 4.0f)
        batch.bodies[clipHeaderColour (base).getARGB()].addWithoutMerging (title);
    batch.edges[(current ? theme::colour::textPrimary.withAlpha (0.55f) : theme::colour::bg0.withAlpha (0.85f)).getARGB()].addWithoutMerging (
        { r.getRight() - 1.0f, r.getY(), 1.0f, r.getHeight() });

    if (c.kind == ClipKind::audio && body.getWidth() >= 3.0f && body.getHeight() >= 3.0f)
    {
        const auto& channel = song.channels[(std::size_t) c.channel];
        const auto path = juce::String::fromUTF8 (channel.samplePath.c_str());
        const auto summary = ui::WaveformCache::getInstance()->find (path, [this, path] { return engine.getSampleLibrary().load (path); });
        if (summary == nullptr)
            return;
        // the same mapping as paintAudioClip
        const auto wave = body.reduced (0.0f, body.getHeight() >= 14.0f ? 2.0f : 1.0f);
        // what the clip plays of the file (its Trim; round 180d): the same frames as the playback
        const auto data = engine.getSampleLibrary().load (path);
        const auto span = data != nullptr ? clipops::clipSpan (channel, data) : precompute::PlayedSpan { 0, (std::int64_t) summary->frames };
        const auto total = (double) span.frames;
        const auto perTick = clipops::framesPerTick (channel, c, total, summary->sampleRate, song.bpm);
        const auto startTick = (double) c.offset + (double) (wave.getX() - r.getX()) / pixelsPerTick;
        const auto forward = ! clipops::playsBackwards (channel, c);
        const auto startFrame = (double) span.first + (forward ? startTick * perTick : total - startTick * perTick);
        const auto colour = clipContentColour (base);
        auto style = ui::currentWaveStyle (colour, juce::jmin (ui::normalisingGain (*summary), 4.0f) * c.gain);
        if (! style.spectral && c.gain > 1.0f)
            style.gain = juce::jmin (style.gain, 8.0f);
        if (! style.spectral)
            ui::appendWaveformPath (batch.waves[colour.getARGB()], physicalScale, wave, wave, *summary, startFrame,
                                    (forward ? perTick : -perTick) / pixelsPerTick, style);
        return;
    }

    if (c.kind == ClipKind::pattern && c.pattern >= 0 && c.pattern < (int) song.patterns.size() && body.getHeight() >= 4.0f)
    {
        // the notes as one-pixel-wide marks at their pitch (the same placement as the full clip)
        const auto& notes = song.patterns[(std::size_t) c.pattern].notes;
        if (notes.empty())
            return;
        int low = 127, high = 0;
        for (const auto& n : notes)
        {
            low = juce::jmin (low, n.key);
            high = juce::jmax (high, n.key);
        }
        const auto inner = body.reduced (0.0f, body.getHeight() >= 12.0f ? 3.0f : 1.0f);
        const auto span = juce::jmax (6, high - low + 2);
        const auto bottomKey = (low + high) / 2.0f - span / 2.0f;
        const auto rows = (float) span + 1.0f;
        const auto noteHeight = juce::jlimit (1.5f, 2.5f, inner.getHeight() / rows);
        auto& marks = batch.contents[clipContentColour (base).getARGB()];
        // what plays: looped along a stretched clip with the LYNX option (issue #25), else once (FL, round 168k)
        const auto passes = clipPasses (c, song.patterns[(std::size_t) c.pattern].effectiveLength (song.beatsPerBar));
        for (auto pass = passes.first; pass <= passes.last; ++pass)
            for (const auto& n : notes)
            {
                const auto at = c.start - c.offset + pass * passes.length + n.start;
                if ((passes.loops && n.start >= passes.length) || at + n.length <= c.start || at >= c.end())
                    continue;
                const auto nx = juce::jmax (inner.getX(), (float) tickToX ((double) at));
                const auto nr = juce::jmin (inner.getRight() - 1.0f, nx + juce::jmax (1.0f, (float) (n.length * pixelsPerTick) - 1.0f));
                if (nr <= nx)
                    continue;
                const auto ny = inner.getBottom() - ((float) n.key - bottomKey + 0.5f) / rows * inner.getHeight() - noteHeight * 0.5f;
                marks.addWithoutMerging ({ nx, ny, nr - nx, noteHeight });
            }
    }
}

namespace
{
// the index of a clip painted from the song's own list (-1 for a copy: a clip being dragged is drawn from one)
int playlistIndexOf (const Song& song, const Clip& c)
{
    const auto* first = song.playlist.data();
    return ! song.playlist.empty() && std::less_equal<const Clip*>() (first, &c) && std::less<const Clip*>() (&c, first + song.playlist.size())
               ? (int) (&c - first)
               : -1;
}
} // namespace

void PlaylistComponent::paintAudioClip (juce::Graphics& g, const Clip& c, juce::Rectangle<float> r)
{
    const uitrace::Scope trace ("Playlist audio clip", false);
    const auto& song = engine.getSong();

    if (c.channel < 0 || c.channel >= (int) song.channels.size())
        return;

    const auto& channel = song.channels[(std::size_t) c.channel];
    const auto colour = audioClipColour (channel);
    const auto name = juce::String::fromUTF8 (channel.name.c_str());
    const auto body = paintClipFrame (g, r, colour, name, ClipGlyph::audio, c.muted);

    // the waveform (ui/WaveformView, analysed in the background): the clean style fills the body with the
    // peak envelope mirrored around the centre, in a light tint of the clip colour
    const auto path = juce::String::fromUTF8 (channel.samplePath.c_str());
    const auto summary = ui::WaveformCache::getInstance()->find (path, [this, path] { return engine.getSampleLibrary().load (path); });

    if (summary != nullptr && body.getHeight() >= 3.0f && body.getWidth() >= 3.0f) // (narrower: the body colour says enough)
    {
        const auto wave = body.reduced (0.0f, body.getHeight() >= 14.0f ? 2.0f : 1.0f);
        // what the clip plays of the file (its Trim; round 180d): the same frames as the playback
        const auto data = engine.getSampleLibrary().load (path);
        const auto span = data != nullptr ? clipops::clipSpan (channel, data) : precompute::PlayedSpan { 0, (std::int64_t) summary->frames };
        const auto total = (double) span.frames;
        const auto perTick = clipops::framesPerTick (channel, c, total, summary->sampleRate, song.bpm);
        // r's left edge is the clip start, i.e. tick `offset` of the audio (counted from the played span's end when
        // the channel is reversed)
        const auto startTick = (double) c.offset + (double) (wave.getX() - r.getX()) / pixelsPerTick;
        const auto forward = ! clipops::playsBackwards (channel, c);
        const auto startFrame = (double) span.first + (forward ? startTick * perTick : total - startTick * perTick);
        auto style = ui::currentWaveStyle (clipContentColour (colour), juce::jmin (ui::normalisingGain (*summary), 4.0f) * c.gain);
        if (! style.spectral && c.gain > 1.0f) // louder clip gain: the shape may hit the top, which is honest
            style.gain = juce::jmin (style.gain, 8.0f);
        const uitrace::Part tally (uitrace::part::waveform);
        // warp markers that bend the tempo (round 175a): each stretch between two markers drawn at its own speed
        auto markers = forward ? warpedit::playedMarkers (channel.sampler, total / summary->sampleRate) : std::vector<WarpPoint> {};
        const auto index = playlistIndexOf (song, c);
        if (drag == Drag::warpMarker && warpClip == index && warpIndex >= 0 && warpIndex < (int) markers.size())
            markers[(std::size_t) warpIndex].beats = warpBeat;
        if (markers.size() >= 3)
        {
            for (std::size_t k = 0; k + 1 < markers.size(); ++k)
            {
                const auto x0 = tickToX (warpedit::clipTickOfBeat (c, markers[k].beats)), x1 = tickToX (warpedit::clipTickOfBeat (c, markers[k + 1].beats));
                const auto part = wave.withLeft (juce::jmax (wave.getX(), (float) x0)).withRight (juce::jmin (wave.getRight(), (float) x1));
                if (part.getWidth() <= 0.0f || x1 <= x0)
                    continue;
                const auto perPixel = (markers[k + 1].seconds - markers[k].seconds) * summary->sampleRate / (x1 - x0);
                const juce::Graphics::ScopedSaveState keep (g);
                g.reduceClipRegion (part.toNearestIntEdges());
                ui::paintWaveform (g, part, *summary, markers[k].seconds * summary->sampleRate + (part.getX() - x0) * perPixel, perPixel, style);
            }
        }
        else
            ui::paintWaveform (g, wave, *summary, startFrame, (forward ? perTick : -perTick) / pixelsPerTick, style);
    }

    paintClipOutline (g, r, colour, false, false);

    // the warp markers: small flags on the body's top edge, the end marker included (the start one never moves)
    {
        const auto index = playlistIndexOf (song, c);
        auto markers = index >= 0 ? clipWarpMarkers (index) : std::vector<WarpPoint> {};
        if (drag == Drag::warpMarker && warpClip == index && warpIndex >= 0 && warpIndex < (int) markers.size())
            markers[(std::size_t) warpIndex].beats = warpBeat;
        // the accent (the audio editor's warp marker colour) with a dark outline: readable on any clip colour (the
        // clip's own content colour was hard to see on pale clips, coordinator's review of 175a M3)
        const auto flag = theme::colour::accent;
        for (std::size_t k = 1; k < markers.size(); ++k)
        {
            const auto x = (float) tickToX (warpedit::clipTickOfBeat (c, markers[k].beats));
            if (x < body.getX() - 0.5f || x > body.getRight() + 0.5f)
                continue;
            const auto active = drag == Drag::warpMarker && warpClip == index && warpIndex == (int) k;
            g.setColour (flag.withAlpha (active ? 0.95f : 0.6f));
            g.drawVerticalLine ((int) std::round (x), body.getY() + 6.0f, body.getBottom());
            juce::Path p;
            p.addTriangle (x - 4.5f, body.getY(), x + 4.5f, body.getY(), x, body.getY() + 7.0f);
            g.setColour (active ? flag.brighter (0.3f) : flag);
            g.fillPath (p);
            g.setColour (theme::colour::bg0.withAlpha (0.8f));
            g.strokePath (p, juce::PathStrokeType (1.0f));
        }
    }

    // fades: a shaded ramp over the body and a handle at the header's corner; an automatic crossfade (round 166e)
    // replaces that side's fade with FL's two crossing S-curves over the overlap
    const auto fadeIn = c.effectiveFadeIn(), fadeOut = c.effectiveFadeOut();
    if (fadeIn > 0 || fadeOut > 0)
    {
        const auto inX = r.getX() + (float) (fadeIn * pixelsPerTick);
        const auto outX = r.getRight() - (float) (fadeOut * pixelsPerTick);
        const auto shade = theme::colour::bg0.withAlpha (0.45f);
        const auto line = clipContentColour (colour).withAlpha (0.9f);
        // the gain curve from x0 to x1 (rising: a fade in), as a line along the body
        auto curve = [&body] (float x0, float x1, bool rising, bool sCurve, bool equalPower) {
            juce::Path p;
            const auto steps = sCurve || equalPower ? juce::jlimit (4, 48, (int) std::abs (x1 - x0) / 3) : 1;
            for (int k = 0; k <= steps; ++k)
            {
                const auto t = (float) k / (float) steps;
                const auto up = rising ? t : 1.0f - t;
                const auto level = equalPower ? std::sin (up * juce::MathConstants<float>::halfPi)
                                 : sCurve     ? clipops::crossfadeCurve (up) : up;
                const juce::Point<float> q { x0 + (x1 - x0) * t, body.getBottom() - level * body.getHeight() };
                if (k == 0)
                    p.startNewSubPath (q);
                else
                    p.lineTo (q);
            }
            return p;
        };
        auto paintFade = [&] (float x0, float x1, bool rising, bool cross, bool equalPower) {
            auto edge = curve (x0, x1, rising, cross, equalPower);
            auto area = edge;
            area.lineTo (x1, body.getY());
            area.lineTo (x0, body.getY());
            area.closeSubPath();
            g.setColour (shade);
            g.fillPath (area);
            g.setColour (line);
            g.strokePath (edge, juce::PathStrokeType (1.2f));
            if (cross || equalPower) // the neighbour's side of the crossfade, which this clip covers
            {
                g.setColour (line.withMultipliedAlpha (0.6f));
                g.strokePath (curve (x0, x1, ! rising, cross, equalPower), juce::PathStrokeType (1.0f));
            }
        };

        if (fadeIn > 0)
            paintFade (body.getX(), inX, true, c.crossfadeIn > 0, (c.fadeCurve & 1) != 0);
        if (fadeOut > 0)
            paintFade (outX, body.getRight(), false, c.crossfadeOut > 0, (c.fadeCurve & 2) != 0);
    }
}

juce::Colour PlaylistComponent::audioClipColour (const ChannelDef& channel)
{
    return channel.colour != 0 ? theme::tame (juce::Colour (channel.colour)) : theme::colour::audioClip;
}

void PlaylistComponent::paintAutomationClip (juce::Graphics& g, const Clip& c, juce::Rectangle<float> r)
{
    const auto& song = engine.getSong();

    if (c.automation < 0 || c.automation >= (int) song.automations.size())
        return;

    const auto& automation = song.automations[(std::size_t) c.automation];
    const auto colour = automation.target.kind == AutomationTargetKind::none ? theme::colour::textDisabled
                                                                              : theme::colour::automation.withMultipliedSaturation (0.75f).withMultipliedBrightness (0.8f);
    paintClipFrame (g, r, colour, juce::String::fromUTF8 (automation.name.c_str()), ClipGlyph::automation, c.muted);
    paintClipOutline (g, r, colour, false, false);

    // automation curve over the clip's range
    const auto body = r.withTrimmedTop (juce::jmin (14.0f, r.getHeight() * 0.4f)).reduced (1.0f, 2.0f); // = automationBody
    const auto clip = g.getClipBounds().toFloat();
    juce::Path curve;
    bool started = false;
    auto lastY = 0.0f;

    // (samples every 2 px on the body's own grid, from a little before the repainted area to a little after it: a
    // repaint of part of the clip draws the same line as a paint of all of it; round 174f)
    const auto lastX = std::min (body.getRight(), clip.getRight() + 4.0f);
    for (auto k = (int) std::floor (juce::jmax (0.0f, clip.getX() - 4.0f - body.getX()) / 2.0f);; ++k)
    {
        const auto x = body.getX() + 2.0f * (float) k;
        if (x > lastX)
            break;
        const auto yAt = [&] (int sx) {
            const auto songTick = xToTick (sx);
            return body.getBottom() - automation.playAt ((double) c.offset + songTick - (double) c.start, songTick) * body.getHeight(); // (with its LFO)
        };
        const auto y = yAt ((int) x);

        if (! started)
            curve.startNewSubPath (x, y);
        else // (round 190bw: Hold, Stairs and Pulse segments keep square corners)
            automationLineTo (curve, x - 2.0f, lastY, x, y, [&] { return yAt ((int) x - 1); });

        lastY = y;
        started = true;
    }

    g.setColour (automation.target.kind == AutomationTargetKind::none ? theme::colour::textSecondary : theme::colour::automation.brighter (0.3f));
    g.strokePath (curve, juce::PathStrokeType (1.5f));

    // round 165: FL's tension handles between the points (hollow circles: drag up / down bends the segment)
    if (r.getWidth() > 24.0f && body.getHeight() >= 10.0f)
        for (int i = 0; i + 1 < (int) automation.points.size(); ++i)
        {
            const auto& a = automation.points[(std::size_t) i];
            const auto& b = automation.points[(std::size_t) i + 1];
            const auto x0 = tickToX ((double) (c.start + a.tick - c.offset)), x1 = tickToX ((double) (c.start + b.tick - c.offset));
            const auto mid = 0.5 * (double) (a.tick + b.tick);
            const auto midX = (float) (0.5 * (x0 + x1));
            if (x1 - x0 < 16.0 || midX < body.getX() || midX > body.getRight())
                continue;
            const auto y = body.getBottom() - automation.valueAt (mid) * body.getHeight();
            g.setColour (theme::colour::automation.brighter (0.2f).withAlpha (0.9f));
            g.drawEllipse (midX - 3.0f, y - 3.0f, 6.0f, 6.0f, 1.2f);
        }

    // the points inside the clip, as handles (edited right here)
    if (r.getWidth() > 24.0f)
        for (const auto& p : automation.points)
        {
            const auto songTick = (double) (c.start + p.tick - c.offset);
            if (songTick < (double) c.start || songTick > (double) c.end())
                continue;
            const auto x = (float) tickToX (songTick);
            const auto y = body.getBottom() - p.value * body.getHeight();
            g.setColour (theme::colour::bg0);
            g.fillEllipse (x - 3.5f, y - 3.5f, 7.0f, 7.0f);
            g.setColour (theme::colour::automation.brighter (0.5f));
            g.fillEllipse (x - 2.5f, y - 2.5f, 5.0f, 5.0f);
        }
}

juce::Rectangle<float> PlaylistComponent::automationBody (const Clip& c) const
{
    const auto x0 = (float) tickToX ((double) c.start), x1 = (float) tickToX ((double) c.end());
    const auto y = (float) trackToY (c.track);
    const auto r = juce::Rectangle<float> (x0, y + 1.0f, juce::jmax (3.0f, x1 - x0 - 1.0f), (float) heightOf (c.track) - 3.0f);
    return r.withTrimmedTop (juce::jmin (14.0f, r.getHeight() * 0.4f)).reduced (1.0f, 2.0f);
}

// --- automation lanes (round 177b) -------------------------------------------------------------------------------
// Optional, off by default (Playlist › Automation lanes). A track's lanes sit under its row; a lane's points are in
// song time. Gestures, the same in every tool: [LMB] adds a point (and drags it), drag a point moves it, [RMB] on a
// point deletes it, [Alt]+drag on the line bends the segment, [RMB] elsewhere opens the lane menu.
int PlaylistComponent::laneHeightOf (const AutomationLane& lane) const
{
    return scaledHeight (lane.height > 0 ? polish::clampTrackHeight (lane.height) : 34);
}

int PlaylistComponent::laneBlockHeight (int track) const
{
    const auto& song = engine.getSong();
    if (! autolanes::shown() || track < 0 || track >= (int) song.playlistTracks.size())
        return 0;
    const auto& t = song.playlistTracks[(std::size_t) track];
    if (t.lanes.empty() || t.lanesFolded || t.folded || noteedit::isFoldedAway (song, track))
        return 0;
    int h = 0;
    for (const auto& lane : t.lanes)
        h += laneHeightOf (lane);
    return h;
}

juce::Rectangle<int> PlaylistComponent::laneRow (int track, int lane) const
{
    if (laneBlockHeight (track) <= 0)
        return {};
    const auto& lanes = engine.getSong().playlistTracks[(std::size_t) track].lanes;
    if (lane < 0 || lane >= (int) lanes.size())
        return {};
    auto y = trackToY (track) + heightOf (track);
    for (int i = 0; i < lane; ++i)
        y += laneHeightOf (lanes[(std::size_t) i]);
    return { 0, y, getWidth(), laneHeightOf (lanes[(std::size_t) lane]) };
}

juce::Rectangle<float> PlaylistComponent::laneBody (int track, int lane) const
{
    const auto row = laneRow (track, lane);
    const auto grid = gridArea();
    return juce::Rectangle<float> ((float) grid.getX(), (float) row.getY() + 4.0f, (float) grid.getWidth(), juce::jmax (2.0f, (float) row.getHeight() - 8.0f));
}

PlaylistComponent::LaneHit PlaylistComponent::laneAt (juce::Point<int> p) const
{
    if (! autolanes::shown() || p.y < headerHeight || p.y >= gridArea().getBottom())
        return {};
    const auto track = yToTrack (p.y);
    if (laneBlockHeight (track) <= 0 || p.y < trackToY (track) + heightOf (track))
        return {};
    const auto& lanes = engine.getSong().playlistTracks[(std::size_t) track].lanes;
    for (int i = 0; i < (int) lanes.size(); ++i)
        if (const auto row = laneRow (track, i); p.y >= row.getY() && p.y < row.getBottom())
            return { track, i };
    return {};
}

int PlaylistComponent::laneAutomation (int track, int lane) const
{
    const auto& song = engine.getSong();
    if (track < 0 || track >= (int) song.playlistTracks.size() || lane < 0 || lane >= (int) song.playlistTracks[(std::size_t) track].lanes.size())
        return -1;
    const auto a = song.playlistTracks[(std::size_t) track].lanes[(std::size_t) lane].automation;
    return a >= 0 && a < (int) song.automations.size() ? a : -1;
}

int PlaylistComponent::lanePointAt (int track, int lane, juce::Point<int> p) const
{
    const auto a = laneAutomation (track, lane);
    if (a < 0)
        return -1;
    const auto body = laneBody (track, lane);
    const auto& points = engine.getSong().automations[(std::size_t) a].points;
    int best = -1;
    auto bestDistance = 6.0f;
    for (int i = 0; i < (int) points.size(); ++i)
    {
        const auto x = (float) tickToX ((double) points[(std::size_t) i].tick);
        const auto y = body.getBottom() - laneShown (a, points[(std::size_t) i].value) * body.getHeight();
        if (const auto d = juce::Point<float> (x, y).getDistanceFrom (p.toFloat()); d <= bestDistance)
        {
            bestDistance = d;
            best = i;
        }
    }
    return best;
}

juce::Rectangle<int> PlaylistComponent::trackLaneArrow (int track) const
{
    const auto& song = engine.getSong();
    if (! autolanes::shown() || track < 0 || track >= (int) song.playlistTracks.size() || song.playlistTracks[(std::size_t) track].lanes.empty()
        || noteedit::isFoldedAway (song, track))
        return {};
    const auto member = noteedit::groupHeaderOf (song, track) >= 0 && ! noteedit::isGroupHeader (song, track);
    const auto left = member ? groupIndent : 2;
    // (the Simple headers: in the first row, beside the M and S buttons, clear of the volume row)
    const auto centreY = simpleHeaders ? trackLed (track).getCentreY() : trackToY (track) + heightOf (track) / 2;
    return { left + 4, centreY - 6, 12, 12 };
}

juce::Rectangle<int> PlaylistComponent::trackAddLane (int track) const
{
    if (! autolanes::shown() || heightOf (track) < 16 || noteedit::isFoldedAway (engine.getSong(), track))
        return {};
    // left of the header's buttons (input, record, group arrow, Simple solo, LED)
    auto right = trackLed (track).getX();
    for (const auto& r : { trackSolo (track), trackArrow (track), trackArm (track), trackInput (track) })
        if (! r.isEmpty())
            right = juce::jmin (right, r.getX());
    const auto led = trackLed (track);
    return { right - 22, led.getCentreY() - 7, 20, 14 };
}

void PlaylistComponent::paintLanes (juce::Graphics& g, juce::Rectangle<int> grid)
{
    const auto& song = engine.getSong();
    if (! autolanes::shown())
        return;
    using namespace theme;
    const auto clipArea = g.getClipBounds();

    for (int t = 0; t < (int) song.playlistTracks.size(); ++t)
    {
        if (laneBlockHeight (t) <= 0)
            continue;
        const auto top = trackToY (t) + heightOf (t);
        if (top > grid.getBottom() || top + laneBlockHeight (t) < grid.getY())
            continue;
        const auto muted = song.isTrackMuted (t);

        for (int i = 0; i < (int) song.playlistTracks[(std::size_t) t].lanes.size(); ++i)
        {
            const auto row = laneRow (t, i);
            if (row.getBottom() < clipArea.getY() || row.getY() > clipArea.getBottom())
                continue;
            const auto a = laneAutomation (t, i);
            if (a < 0)
                continue;
            const auto& automation = song.automations[(std::size_t) a];
            const auto body = laneBody (t, i);

            // where a clip of the same target plays, the clip wins: those spans are shaded
            for (const auto& c : song.playlist)
            {
                if (c.kind != ClipKind::automation || c.muted || song.isTrackMuted (c.track) || c.automation < 0
                    || c.automation >= (int) song.automations.size() || c.automation == a
                    || ! (song.automations[(std::size_t) c.automation].target == automation.target))
                    continue;
                const auto x0 = (float) tickToX ((double) c.start), x1 = (float) tickToX ((double) c.end());
                if (x1 < (float) grid.getX() || x0 > (float) grid.getRight())
                    continue;
                g.setColour (colour::bg0.withAlpha (0.45f));
                g.fillRect (juce::Rectangle<float> (x0, (float) row.getY(), x1 - x0, (float) row.getHeight() - 1.0f));
            }

            const auto line = muted ? colour::textDisabled : colour::automation.brighter (0.3f);
            juce::Path curve, fill;
            bool started = false;
            const auto from = (float) juce::jmax (grid.getX(), clipArea.getX()), to = (float) juce::jmin (grid.getRight(), clipArea.getRight());
            auto lastY = 0.0f;
            for (auto x = from; x <= to; x += 2.0f)
            {
                const auto yAt = [&] (int sx) { return body.getBottom() - laneShown (a, automation.valueAt (xToTick (sx))) * body.getHeight(); };
                const auto y = yAt ((int) x);
                if (! started)
                {
                    curve.startNewSubPath (x, y);
                    fill.startNewSubPath (x, body.getBottom());
                    curve.lineTo (x, y);
                    fill.lineTo (x, y);
                }
                else // (round 190bw: Hold, Stairs and Pulse segments keep square corners)
                {
                    automationLineTo (curve, x - 2.0f, lastY, x, y, [&] { return yAt ((int) x - 1); });
                    automationLineTo (fill, x - 2.0f, lastY, x, y, [&] { return yAt ((int) x - 1); });
                }
                lastY = y;
                started = true;
            }
            if (started)
            {
                fill.lineTo (to, body.getBottom());
                fill.closeSubPath();
                g.setColour (line.withAlpha (0.10f));
                g.fillPath (fill);
                g.setColour (line);
                g.strokePath (curve, juce::PathStrokeType (1.5f));
            }

            for (const auto& p : automation.points)
            {
                const auto x = (float) tickToX ((double) p.tick);
                if (x < from - 4.0f || x > to + 4.0f)
                    continue;
                const auto y = body.getBottom() - laneShown (a, p.value) * body.getHeight();
                g.setColour (colour::bg0);
                g.fillEllipse (x - 3.5f, y - 3.5f, 7.0f, 7.0f);
                g.setColour (muted ? colour::textDisabled : colour::automation.brighter (0.5f));
                g.fillEllipse (x - 2.5f, y - 2.5f, 5.0f, 5.0f);
            }
        }
    }
}

void PlaylistComponent::paintLaneHeaders (juce::Graphics& g, juce::Rectangle<int> headers)
{
    const auto& song = engine.getSong();
    if (! autolanes::shown())
        return;
    using namespace theme;

    for (int t = 0; t < (int) song.playlistTracks.size(); ++t)
    {
        if (laneBlockHeight (t) <= 0)
            continue;
        const auto member = noteedit::groupHeaderOf (song, t) >= 0 && ! noteedit::isGroupHeader (song, t);
        const auto left = (float) (member ? groupIndent : 2) + 10.0f; // indented under the track
        for (int i = 0; i < (int) song.playlistTracks[(std::size_t) t].lanes.size(); ++i)
        {
            const auto row = laneRow (t, i);
            if (row.getBottom() < headers.getY() || row.getY() > headers.getBottom())
                continue;
            const auto a = laneAutomation (t, i);
            const auto plate = juce::Rectangle<float> (left, (float) row.getY() + 1.0f, (float) headerWidth - 2.0f - left, (float) row.getHeight() - 3.0f);
            g.setColour (colour::bg2.interpolatedWith (colour::automation, 0.08f));
            g.fillRoundedRectangle (plate, radius::sm);
            g.setColour (colour::automation.withAlpha (song.isTrackMuted (t) ? 0.3f : 0.9f));
            g.fillRoundedRectangle (plate.withWidth (3.0f).reduced (0.0f, 3.0f), 1.5f);
            g.setColour (song.isTrackMuted (t) ? colour::textDisabled : colour::textSecondary);
            g.setFont (font (type::caption));
            textcache::drawFittedText (g, a >= 0 ? juce::String::fromUTF8 (song.automations[(std::size_t) a].name.c_str()) : juce::String(),
                                       (int) plate.getX() + 7, row.getY(), (int) plate.getWidth() - 10, row.getHeight(), juce::Justification::centredLeft,
                                       row.getHeight() >= 30 ? 2 : 1, 0.8f);
        }
    }
}

bool PlaylistComponent::dragOutToClipGrid (const juce::MouseEvent& e)
{
    // only in the Clip-launch layout, with the mouse off the Playlist and over the clip grid beside it
    if (layoutstyle::current() != layoutstyle::Style::clipLaunch || getLocalBounds().contains (e.getPosition()))
        return false;
    const auto& song = engine.getSong();
    if (dragClip < 0 || dragClip >= (int) song.playlist.size() || dragIndices.size() != dragOriginals.size())
        return false;
    const auto kind = song.playlist[(std::size_t) dragClip].kind;
    if (kind != ClipKind::pattern && kind != ClipKind::audio)
        return false;
    if (! isClipGrid (juce::Desktop::getInstance().findComponentAt (e.getScreenPosition())))
        return false;
    auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this);
    if (container == nullptr || container->isDragAndDropActive())
        return false;
    engine.editSong ([this] (Song& s) {
        for (std::size_t j = 0; j < dragIndices.size(); ++j)
            if (const auto i = dragIndices[j]; i >= 0 && i < (int) s.playlist.size())
                s.playlist[(std::size_t) i] = dragOriginals[j];
    });
    juce::Image image (juce::Image::ARGB, 96, 22, true);
    {
        juce::Graphics g (image);
        g.setColour (theme::colour::accent.withAlpha (0.85f));
        g.fillRoundedRectangle (image.getBounds().toFloat().reduced (1.0f), 3.0f);
    }
    container->startDragging (playlistClipDragDescription (dragClip), this, juce::ScaledImage (image), false);
    drag = Drag::none;
    repaint();
    return true;
}

bool PlaylistComponent::laneMouseDown (const juce::MouseEvent& e)
{
    const auto hit = laneAt (e.getPosition());
    if (hit.track < 0)
        return false;
    const auto a = laneAutomation (hit.track, hit.lane);
    if (a < 0)
        return true;

    if (e.x < headerWidth) // the lane's header: its menu; a double click opens the point editor
    {
        if (flmods::popup (e.mods))
            showLaneMenu (hit.track, hit.lane);
        else if (e.getNumberOfClicks() >= 2)
            openAutomationEditor (engine, a);
        return true;
    }

    const auto& points = engine.getSong().automations[(std::size_t) a].points;
    const auto point = lanePointAt (hit.track, hit.lane, e.getPosition());
    const auto body = laneBody (hit.track, hit.lane);

    // round 190ef, Draw Mode (the Clip-launch layout's B, core/DrawMode.h): a press draws the cell under the mouse at the
    // grid width, a drag goes on drawing (freehand without a grid or with Alt, the Playlist's "no snap"); the right button
    // keeps its point menu
    if (drawmode::isOn() && ! flmods::popup (e.mods) && e.mods.isLeftButtonDown())
    {
        laneTrack = hit.track;
        laneIndex = hit.lane;
        setSelection ({});
        drag = Drag::laneDraw;
        drawFreehand = grid::resolve (snapBox).kind == grid::Kind::none || gridTicks() <= 1 || e.mods.isAltDown();
        drawTick = juce::jmax<std::int64_t> (0, (std::int64_t) std::floor (xToTick (e.x)));
        drawValue = laneValueOf (a, juce::jlimit (0.0f, 1.0f, (body.getBottom() - (float) e.y) / juce::jmax (1.0f, body.getHeight())));
        laneDrawTo (e);
        return true;
    }

    if (flmods::popup (e.mods) || (e.mods.isAltDown() && point >= 0)) // on a point: delete it (FL's Alt+click too)
    {
        if (point >= 0 && points.size() > 1)
            engine.editSong ([a, point] (Song& s) {
                eraseAutomationPoint (s.automations[(std::size_t) a].points, point);
            });
        else if (point < 0)
            showLaneMenu (hit.track, hit.lane);
        return true;
    }
    if (! e.mods.isLeftButtonDown())
        return true;

    laneTrack = hit.track;
    laneIndex = hit.lane;
    setSelection ({});

    if (point >= 0)
    {
        drag = Drag::lanePoint;
        autoPoint = point;
        return true;
    }

    const auto tick = (double) juce::jmax<std::int64_t> (0, (std::int64_t) xToTick (e.x));
    if (e.mods.isAltDown()) // [Alt]+drag on the line bends the segment under the mouse
    {
        for (int i = 0; i + 1 < (int) points.size(); ++i)
            if ((double) points[(std::size_t) i].tick <= tick && tick < (double) points[(std::size_t) i + 1].tick)
            {
                drag = Drag::laneTension;
                autoPoint = i;
                tensionStart = points[(std::size_t) i].tension;
                tensionSign = points[(std::size_t) i + 1].value >= points[(std::size_t) i].value ? -1.0f : 1.0f;
                return true;
            }
        return true;
    }

    // a new point here (snapped; [Shift]: on the line), then dragged
    AutomationPoint p;
    p.tick = juce::jmax<std::int64_t> (0, snap (xToTick (e.x), true));
    p.value = e.mods.isShiftDown() ? engine.getSong().automations[(std::size_t) a].valueAt ((double) p.tick)
                                   : laneValueOf (a, juce::jlimit (0.0f, 1.0f, (body.getBottom() - (float) e.y) / juce::jmax (1.0f, body.getHeight())));
    int added = -1;
    engine.editSong ([a, p, &added] (Song& s) { added = insertAutomationPoint (s.automations[(std::size_t) a].points, p); });
    drag = Drag::lanePoint;
    autoPoint = added;
    return true;
}

// round 190ef: one move of a Draw Mode drag, from the last position to the mouse (one undo step per press: editSong
// joins the edits made while the button is held)
void PlaylistComponent::laneDrawTo (const juce::MouseEvent& e)
{
    const auto a = laneAutomation (laneTrack, laneIndex);
    if (a < 0)
        return;
    const auto body = laneBody (laneTrack, laneIndex);
    const auto tick = juce::jmax<std::int64_t> (0, (std::int64_t) std::floor (xToTick (e.x)));
    const auto value = laneValueOf (a, juce::jlimit (0.0f, 1.0f, (body.getBottom() - (float) e.y) / juce::jmax (1.0f, body.getHeight())));
    const auto from = drawTick;
    const auto fromValue = drawValue;
    if (drawFreehand)
        engine.editSong ([a, from, fromValue, tick, value] (Song& s) {
            drawmode::drawLine (s.automations[(std::size_t) a].points, from, fromValue, tick, value);
        });
    else
    {
        const auto steps = drawmode::stepsAlong (from, fromValue, tick, value, gridTicks());
        engine.editSong ([a, steps] (Song& s) {
            for (const auto& step : steps)
                drawmode::drawStep (s.automations[(std::size_t) a].points, step.cell.from, step.cell.to, step.value);
        });
    }
    drawTick = tick;
    drawValue = value;
}

// round 190cg: a tempo lane shows the song's own range (Song::tempoViewLow / High, the reference's lowest / highest BPM
// of the tempo lane) when one is set; every other lane, and a tempo lane without a range, shows 0 … 1 as before
float PlaylistComponent::laneShown (int automation, float value) const
{
    const auto& song = engine.getSong();
    if (automation < 0 || automation >= (int) song.automations.size() || song.automations[(std::size_t) automation].target.kind != AutomationTargetKind::tempo
        || ! (song.tempoViewHigh > song.tempoViewLow))
        return value;
    return (float) juce::jlimit (0.0, 1.0, (tempoFromValue (value) - song.tempoViewLow) / (song.tempoViewHigh - song.tempoViewLow));
}

float PlaylistComponent::laneValueOf (int automation, float shown) const
{
    const auto& song = engine.getSong();
    if (automation < 0 || automation >= (int) song.automations.size() || song.automations[(std::size_t) automation].target.kind != AutomationTargetKind::tempo
        || ! (song.tempoViewHigh > song.tempoViewLow))
        return shown;
    return juce::jlimit (0.0f, 1.0f, valueFromTempo (song.tempoViewLow + (double) shown * (song.tempoViewHigh - song.tempoViewLow)));
}

void PlaylistComponent::showLaneMenu (int track, int lane)
{
    const auto a = laneAutomation (track, lane);
    if (a < 0)
        return;
    juce::PopupMenu menu;
    menu.addSectionHeader (juce::String::fromUTF8 (engine.getSong().automations[(std::size_t) a].name.c_str()));
    if (engine.getSong().automations[(std::size_t) a].target.kind == AutomationTargetKind::tempo)
    {
        // the lane's range [xml TempoAutomationViewBottom / Top: 60 / 200 in a new set of the reference]
        juce::PopupMenu range;
        const auto& song = engine.getSong();
        const auto whole = ! (song.tempoViewHigh > song.tempoViewLow);
        range.addItem (tr ("Whole range"), true, whole, [this] { engine.editSong ([] (Song& s) { s.tempoViewLow = s.tempoViewHigh = 0.0; }); });
        for (const auto [low, high] : { std::pair { 60.0, 200.0 }, { 40.0, 120.0 }, { 80.0, 160.0 }, { 100.0, 200.0 }, { 20.0, 300.0 } })
            range.addItem (juce::String ((int) low) + " - " + juce::String ((int) high) + " BPM", true,
                           ! whole && song.tempoViewLow == low && song.tempoViewHigh == high,
                           [this, low = low, high = high] { engine.editSong ([low, high] (Song& s) { s.tempoViewLow = low; s.tempoViewHigh = high; }); });
        range.addItem (tr ("Set the range..."), [this] { askTempoRange(); });
        menu.addSubMenu (tr ("Tempo range shown"), range);
    }
    menu.addItem (tr ("Edit points..."), [this, a] { openAutomationEditor (engine, a); });
    menu.addItem (tr ("To automation clip"), [this, track, lane] {
        engine.editSong ([track, lane] (Song& s) { autolanes::laneToClip (s, track, lane); });
    });
    menu.addItem (tr ("Remove the lane"), [this, track, lane] {
        engine.editSong ([track, lane] (Song& s) { autolanes::removeLane (s, track, lane); });
    });
    menu.addSeparator();
    menu.addItem (tr ("Fold the lanes"), [this, track] { editTrack (track, [] (PlaylistTrack& t) { t.lanesFolded = true; }); });
    menu.addItem (tr ("Hide all lanes"), [this] { toggleAutomationLanes(); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

void PlaylistComponent::showAddLaneMenu (int track)
{
    const auto& song = engine.getSong();
    std::vector<AutomationTarget> targets;
    juce::PopupMenu menu;
    auto add = [&] (const AutomationTarget& target) {
        if (target.kind == AutomationTargetKind::none || std::find (targets.begin(), targets.end(), target) != targets.end())
            return;
        targets.push_back (target);
        menu.addItem (describeAutomationTarget (engine, target), [this, target, track] { createAutomationLane (engine, target, track); });
    };

    // the track's own channel and mixer track first
    const auto* info = track >= 0 && track < (int) song.playlistTracks.size() ? &song.playlistTracks[(std::size_t) track] : nullptr;
    if (info != nullptr && info->mode == PlaylistTrackMode::instrument && info->channel >= 0 && info->channel < (int) song.channels.size())
    {
        menu.addSectionHeader (tr ("This track"));
        add ({ AutomationTargetKind::channelVolume, info->channel, 0, 0 });
        add ({ AutomationTargetKind::channelPan, info->channel, 0, 0 });
    }
    if (info != nullptr && info->mode != PlaylistTrackMode::unassigned && info->mixerTrack > 0 && info->mixerTrack < (int) song.mixer.size())
    {
        if (targets.empty())
            menu.addSectionHeader (tr ("This track"));
        add ({ AutomationTargetKind::mixerVolume, info->mixerTrack, 0, 0 });
        add ({ AutomationTargetKind::mixerPan, info->mixerTrack, 0, 0 });
    }
    const auto before = targets.size();
    for (int which = 0; which < 2; ++which)
        if (const auto last = engine.getLastTweaked (which); last.has_value() && std::find (targets.begin(), targets.end(), *last) == targets.end())
        {
            if (targets.size() == before)
                menu.addSectionHeader (tr ("Last tweaked"));
            add (*last);
        }
    menu.addSectionHeader (tr ("Song"));
    add ({ AutomationTargetKind::tempo, 0, 0, 0 });
    add ({ AutomationTargetKind::mixerVolume, 0, 0, 0 });
    menu.addSeparator();
    menu.addItem (tr ("Any control: right-click it > Automate in lane"), false, false, [] {});
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

void PlaylistComponent::toggleAutomationLanes()
{
    autolanes::setShown (! autolanes::shown());
    refreshTrackLayout();
    updateScrollBars();
    repaint();
    if (onViewOptionChanged)
        onViewOptionChanged();
}

int PlaylistComponent::automationPointAt (int clip, juce::Point<int> pos) const
{
    const auto& song = engine.getSong();

    if (clip < 0 || clip >= (int) song.playlist.size() || song.playlist[(std::size_t) clip].kind != ClipKind::automation)
        return -1;

    const auto& c = song.playlist[(std::size_t) clip];
    if (c.automation < 0 || c.automation >= (int) song.automations.size())
        return -1;

    const auto body = automationBody (c);
    const auto& points = song.automations[(std::size_t) c.automation].points;

    for (int i = (int) points.size() - 1; i >= 0; --i)
    {
        const auto songTick = (double) (c.start + points[(std::size_t) i].tick - c.offset);
        if (songTick < (double) c.start || songTick > (double) c.end())
            continue;
        const juce::Point<float> at ((float) tickToX (songTick), body.getBottom() - points[(std::size_t) i].value * body.getHeight());
        if (at.getDistanceFrom (pos.toFloat()) <= 6.0f)
            return i;
    }

    return -1;
}

bool PlaylistComponent::isOnAutomationCurve (int clip, juce::Point<int> pos) const
{
    const auto& song = engine.getSong();

    if (clip < 0 || clip >= (int) song.playlist.size() || song.playlist[(std::size_t) clip].kind != ClipKind::automation)
        return false;

    const auto& c = song.playlist[(std::size_t) clip];
    if (c.automation < 0 || c.automation >= (int) song.automations.size())
        return false;

    const auto body = automationBody (c);
    if (! body.expanded (0.0f, 4.0f).contains (pos.toFloat()))
        return false;

    const auto value = song.automations[(std::size_t) c.automation].valueAt ((double) c.offset + xToTick (pos.x) - (double) c.start);
    return std::abs (body.getBottom() - value * body.getHeight() - (float) pos.y) <= 5.0f;
}

bool PlaylistComponent::automationEditable (int clip) const
{
    const auto& song = engine.getSong();
    if (clip < 0 || clip >= (int) song.playlist.size())
        return false;
    const auto& c = song.playlist[(std::size_t) clip];
    if (c.kind != ClipKind::automation || c.automation < 0 || c.automation >= (int) song.automations.size())
        return false;
    const auto body = automationBody (c);
    return body.getWidth() > 24.0f && body.getHeight() >= 10.0f;
}

juce::Point<float> PlaylistComponent::automationHandlePos (int clip, int point) const
{
    if (! automationEditable (clip))
        return { -1.0f, -1.0f };
    const auto& song = engine.getSong();
    const auto& c = song.playlist[(std::size_t) clip];
    const auto& automation = song.automations[(std::size_t) c.automation];
    if (point < 0 || point + 1 >= (int) automation.points.size())
        return { -1.0f, -1.0f };
    const auto& a = automation.points[(std::size_t) point];
    const auto& b = automation.points[(std::size_t) point + 1];
    const auto x0 = tickToX ((double) (c.start + a.tick - c.offset)), x1 = tickToX ((double) (c.start + b.tick - c.offset));
    const auto body = automationBody (c);
    const auto midX = (float) (0.5 * (x0 + x1));
    if (x1 - x0 < 16.0 || midX < body.getX() || midX > body.getRight())
        return { -1.0f, -1.0f };
    return { midX, body.getBottom() - automation.valueAt (0.5 * (double) (a.tick + b.tick)) * body.getHeight() };
}

int PlaylistComponent::automationHandleAt (int clip, juce::Point<int> p) const
{
    if (! automationEditable (clip))
        return -1;
    const auto& song = engine.getSong();
    const auto n = (int) song.automations[(std::size_t) song.playlist[(std::size_t) clip].automation].points.size();
    for (int i = 0; i + 1 < n; ++i)
        if (const auto h = automationHandlePos (clip, i); h.x >= 0.0f && h.getDistanceFrom (p.toFloat()) <= 5.0f)
            return i;
    return -1;
}

void PlaylistComponent::showAutomationPointMenu (int clip, int point)
{
    auto menu = getAutomationPointMenuForSnapshot (clip, point);
    if (menu.getNumItems() > 0)
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

juce::PopupMenu PlaylistComponent::getAutomationPointMenuForSnapshot (int clip, int point)
{
    // FL's point menu [RES FLEngine TFruityLoopsMainForm.EEACPointMenu]: Delete, the Mode block (13 curve modes, round
    // 190bw), then Copy value / Paste value / Type in value...; LYNX's own commands follow
    const auto& song = engine.getSong();
    if (clip < 0 || clip >= (int) song.playlist.size())
        return {};
    const auto a = song.playlist[(std::size_t) clip].automation;
    if (a < 0 || a >= (int) song.automations.size() || point < 0 || point >= (int) song.automations[(std::size_t) a].points.size())
        return {};
    const auto& automation = song.automations[(std::size_t) a];
    const auto value = automation.points[(std::size_t) point].value;
    juce::PopupMenu menu;
    menu.addSectionHeader (automationPointMenuHeader (point, (int) automation.points.size())); // FL: "Point 2/14"
    menu.addItem (tr ("Delete"), automation.points.size() > 1, false, [this, a, point] {
        engine.editSong ([a, point] (Song& s) {
            auto& points = s.automations[(std::size_t) a].points;
            if (points.size() > 1)
                eraseAutomationPoint (points, point);
        });
    });
    addAutomationModeItems (menu, automationPointMode (automation.points, point), point > 0, [this, a, point] (int mode) {
        engine.editSong ([a, point, mode] (Song& s) { setAutomationPointMode (s.automations[(std::size_t) a].points, point, mode); });
    });
    menu.addSeparator();
    menu.addItem (tr ("Copy value"), [value] { copiedAutomationValue() = value; });
    menu.addItem (tr ("Paste value"), copiedAutomationValue() >= 0.0f, false, [this, a, point] {
        engine.editSong ([a, point, v = copiedAutomationValue()] (Song& s) {
            auto& points = s.automations[(std::size_t) a].points;
            if (point < (int) points.size())
                points[(std::size_t) point].value = v;
        });
    });
    menu.addItem (tr ("Type in value..."), [this, a, point] { typeAutomationPointValue (engine, a, point); });
    menu.addSeparator();
    menu.addItem (tr ("Straight segment (reset the curve)"), point + 1 < (int) automation.points.size(), false, [this, a, point] {
        engine.editSong ([a, point] (Song& s) { setAutomationSegmentTension (s.automations[(std::size_t) a].points, point, 0.0f); });
    });
    menu.addItem (tr ("Edit points..."), [this, a] { openAutomationEditor (engine, a); });
    return menu;
}

double PlaylistComponent::clipFileSeconds (int index) const
{
    const auto& song = engine.getSong();
    if (index < 0 || index >= (int) song.playlist.size() || song.playlist[(std::size_t) index].kind != ClipKind::audio)
        return 0.0;
    const auto channel = song.playlist[(std::size_t) index].channel;
    if (channel < 0 || channel >= (int) song.channels.size())
        return 0.0;
    const auto path = juce::String::fromUTF8 (song.channels[(std::size_t) channel].samplePath.c_str());
    if (const auto data = engine.getSampleLibrary().findLoaded (path); data != nullptr && data->sampleRate > 0.0)
        return (double) data->frames() / data->sampleRate;
    // (not held by the engine: the waveform's own summary, as the clip is painted from it)
    if (const auto summary = ui::WaveformCache::getInstance()->find (path); summary != nullptr && summary->sampleRate > 0.0)
        return (double) summary->frames / summary->sampleRate;
    return 0.0;
}

std::vector<WarpPoint> PlaylistComponent::clipWarpMarkers (int index) const
{
    const auto& song = engine.getSong();
    if (index < 0 || index >= (int) song.playlist.size())
        return {};
    const auto& c = song.playlist[(std::size_t) index];
    if (c.kind != ClipKind::audio || c.channel < 0 || c.channel >= (int) song.channels.size()
        || clipops::playsBackwards (song.channels[(std::size_t) c.channel], c))
        return {};
    return warpedit::playedMarkers (song.channels[(std::size_t) c.channel].sampler, clipFileSeconds (index));
}

int PlaylistComponent::warpMarkerAt (int index, juce::Point<int> p) const
{
    const auto markers = clipWarpMarkers (index);
    if (markers.size() < 3)
        return -1;
    const auto& c = engine.getSong().playlist[(std::size_t) index];
    const auto top = (float) clipBounds (index).getY() + (float) clipTitleHeight;
    if (p.y < top - 1.0f || p.y > top + 8.0f)
        return -1;
    int best = -1;
    double bestDx = 5.0;
    for (std::size_t k = 1; k < markers.size(); ++k)
        if (const auto dx = std::abs (tickToX (warpedit::clipTickOfBeat (c, markers[k].beats)) - p.x); dx <= bestDx)
        {
            bestDx = dx;
            best = (int) k;
        }
    return best;
}

int PlaylistComponent::fadeHandleAt (int index, juce::Point<int> p) const
{
    const auto& c = engine.getSong().playlist[(std::size_t) index];

    if (c.kind != ClipKind::audio)
        return 0;

    const auto y = trackToY (c.track) + 1 + 15;
    const auto inX = tickToX ((double) c.start) + c.fadeIn * pixelsPerTick;
    const auto outX = tickToX ((double) c.end()) - c.fadeOut * pixelsPerTick;

    if (std::abs (p.y - y) > 6)
        return 0;
    if (std::abs (p.x - (inX + 2.0)) <= 6.0)
        return 1;
    if (std::abs (p.x - (outX - 2.0)) <= 6.0)
        return 2;
    return 0;
}

juce::Rectangle<float> PlaylistComponent::gainHandle (int index) const
{
    const auto& song = engine.getSong();
    if (index < 0 || index >= (int) song.playlist.size() || song.playlist[(std::size_t) index].kind != ClipKind::audio)
        return {};
    const auto r = clipBounds (index).toFloat();
    if (r.getWidth() < 30.0f || r.getHeight() - clipTitleHeight < 16.0f)
        return {};
    // FL 2026: a semicircle sitting on the clip's bottom edge, in the middle
    constexpr float radius = 5.0f;
    return { r.getCentreX() - radius, r.getBottom() - radius - 1.0f, radius * 2.0f, radius + 1.0f };
}

void PlaylistComponent::showGainMenu (int index)
{
    if (index < 0 || index >= (int) engine.getSong().playlist.size())
        return;
    gainMenu (index).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

juce::PopupMenu PlaylistComponent::gainMenu (int index)
{
    const auto& song = engine.getSong();
    juce::PopupMenu menu;
    if (index < 0 || index >= (int) song.playlist.size())
        return menu;
    std::vector<int> targets;
    for (auto i : isSelected (index) ? selection : std::vector<int> { index })
        if (i >= 0 && i < (int) song.playlist.size() && song.playlist[(std::size_t) i].kind == ClipKind::audio)
            targets.push_back (i);
    const auto db = clipops::clipGainToDb (song.playlist[(std::size_t) index].gain);
    auto setDb = [this, targets] (double value) {
        const auto g = clipops::clipGainFromDb (value);
        engine.editSong ([targets, g] (Song& s) {
            for (auto i : targets)
                if (i < (int) s.playlist.size())
                    s.playlist[(std::size_t) i].gain = g;
        });
    };

    // FL 2026's gain handle menu [SHOT round 166e]
    menu.addSectionHeader (tr ("Gain: {0}", { gainText (song.playlist[(std::size_t) index].gain) }));
    menu.addItem (tr ("Reset gain"), [setDb] { setDb (0.0); });
    // FL's popup (FUN_00cfcdf0, handle kind 5): with two or more clips selected and the clicked one among them, Normalize
    // reads "Normalize selection individually" and "Normalize selection as a group" appears (round 189n)
    const auto many = selection.size() >= 2 && isSelected (index);
    menu.addItem (many ? tr ("Normalize selection individually") : tr ("Normalize"), [this, targets] { normalizeClips (targets, false); });
    if (many)
        menu.addItem (tr ("Normalize selection as a group"), [this] { normalizeClips (selection, true); });
    menu.addSeparator();
    menu.addItem (tr ("Copy value"), [db] { copiedGainDb = db; hasCopiedGain = true; });
    menu.addItem (tr ("Paste value"), hasCopiedGain, false, [setDb] { setDb (copiedGainDb); });
    menu.addItem (tr ("Type in value..."), [db, setDb] {
        auto* window = new juce::AlertWindow (tr ("Clip gain"), tr ("Decibels ({0} to {1})", { juce::String (clipops::clipGainMinDb, 1), "+" + juce::String (clipops::clipGainMaxDb, 0) }),
                                              juce::MessageBoxIconType::NoIcon);
        window->addTextEditor ("value", juce::String (db, 2));
        window->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        window->enterModalState (true, juce::ModalCallbackFunction::create ([window, setDb] (int result) {
                                     if (result != 1)
                                         return;
                                     const auto typed = window->getTextEditorContents ("value").trim();
                                     setDb (typed.containsIgnoreCase ("inf") ? clipops::clipGainMinDb
                                                                             : typed.retainCharacters ("0123456789.,-+").replaceCharacter (',', '.').getDoubleValue());
                                 }),
                                 true);
    });
    return menu;
}

// FL's fade handle menu (EventEditForm PLFadeHandleMenu, PLFadeHandleMenuClick 0xea9040 -> FUN_00d01bc0; checks from its
// popup FUN_00cfcdf0, round 189i; FL 2026's own menu compared in round 189l): on the clicked clip, or on every selected
// audio clip when it is selected; `side` 0 = the fade in, 1 = the fade out. Create crossfade: round 189n. (Normalize and
// Normalize selection as a group belong to the gain handle's menu, showGainMenu.)
void PlaylistComponent::showFadeHandleMenu (int index, int side)
{
    if (index < 0 || index >= (int) engine.getSong().playlist.size())
        return;
    fadeHandleMenu (index, side).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

juce::PopupMenu PlaylistComponent::fadeHandleMenu (int index, int side, bool clipMenu)
{
    const auto& song = engine.getSong();
    juce::PopupMenu menu;
    if (index < 0 || index >= (int) song.playlist.size())
        return menu;
    std::vector<int> targets;
    for (auto i : isSelected (index) ? selection : std::vector<int> { index })
        if (i >= 0 && i < (int) song.playlist.size() && song.playlist[(std::size_t) i].kind == ClipKind::audio)
            targets.push_back (i);
    const auto& clip = song.playlist[(std::size_t) index];
    const auto msPerTick = 60000.0 / (juce::jmax (1.0, song.bpm) * ppq);
    const auto lengthMs = (double) (side == 0 ? clip.fadeIn : clip.fadeOut) * msPerTick;
    auto edit = [this, targets] (std::function<void (Clip&)> change) {
        engine.editSong ([targets, change] (Song& s) {
            for (auto i : targets)
                if (i < (int) s.playlist.size())
                    change (s.playlist[(std::size_t) i]);
        });
        repaint();
    };
    auto setMs = [edit, side, msPerTick] (double ms) {
        edit ([side, ticks = (std::int64_t) std::llround (juce::jmax (0.0, ms) / msPerTick)] (Clip& c) {
            auto& fade = side == 0 ? c.fadeIn : c.fadeOut;
            fade = juce::jlimit<std::int64_t> (0, c.length, ticks);
            if (fade > 0)
                clipops::setManualFade (c, side, true); // a set fade is a manual one (FL's fade flags bit 0 / 1)
        });
    };
    // FL 2026's fade handle menu (GUI, rounds 189l / 189n: shots FL-fade-in-menu-overlap.png): "Reset fade in" / "Reset fade
    // out" even on a crossfaded side, then "Create crossfade", enabled when the side has a clip to cross (FUN_00d023d0).
    // ("Reset ... tension" belongs to FL's tension handle, the circle in a fade, which LYNX's clip fades don't have.)
    // (round 190ey, live check 190ew PL-7: the clip menu's "Fade in / out curve" submenu reads "Reset", then the gain
    // handle's Normalize items; core flclipmenu::fadeCurve)
    menu.addItem (clipMenu ? tr ("Reset") : side == 0 ? tr ("Reset fade in") : tr ("Reset fade out"), [edit, side] {
        edit ([side] (Clip& c) {
            (side == 0 ? c.fadeIn : c.fadeOut) = 0;
            (side == 0 ? c.fadeInTension : c.fadeOutTension) = 0.0f;
        });
    });
    if (clipMenu)
    {
        menu.addItem (tr ("Normalize"), [this, targets] { normalizeClips (targets, false); });
        menu.addItem (tr ("Normalize selection as a group"), [this, targets] { normalizeClips (targets, true); });
    }
    std::vector<int> menuClips; // FL's FUN_00cf9770: the selection when the clicked clip is in it, else the clicked clip
    for (auto i : isSelected (index) ? selection : std::vector<int> { index })
        if (i >= 0 && i < (int) song.playlist.size())
            menuClips.push_back (i);
    menu.addItem (tr ("Create crossfade"), clipops::canCreateCrossfade (song.playlist, menuClips, side, selection), false,
                  [this, index, side, chosen = selection] {
                      engine.editSong ([index, side, chosen] (Song& s) { clipops::createCrossfades (s.playlist, index, side, chosen); });
                      repaint();
                  });
    if (! clipMenu)
    {
        menu.addItem (tr ("Copy value"), [lengthMs] { copiedFadeMs = lengthMs; hasCopiedFade = true; });
        menu.addItem (tr ("Paste value"), hasCopiedFade, false, [setMs] { setMs (copiedFadeMs); });
        menu.addItem (tr ("Type in value..."), [lengthMs, setMs] {
            auto* window = new juce::AlertWindow (tr ("Fade"), tr ("Milliseconds"), juce::MessageBoxIconType::NoIcon);
            window->addTextEditor ("value", juce::String (lengthMs, 1));
            window->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
            window->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
            window->enterModalState (true, juce::ModalCallbackFunction::create ([window, setMs] (int result) {
                                         if (result == 1)
                                             setMs (window->getTextEditorContents ("value").retainCharacters ("0123456789.,").replaceCharacter (',', '.').getDoubleValue());
                                     }),
                                     true);
        });
    }
    // (the clip menu shows Manual greyed, with the side's state: FL's faded tick)
    menu.addItem (tr ("Manual"), ! clipMenu, clipops::manualFade (clip, side), [edit, side, on = ! clipops::manualFade (clip, side)] {
        edit ([side, on] (Clip& c) { clipops::setManualFade (c, side, on); });
    });
    menu.addSectionHeader (tr ("Declicker"));
    const auto choice = clipops::declickChoice (clip, side);
    const auto addChoice = [&] (const juce::String& text, int value, bool ticked) {
        menu.addItem (text, true, ticked, [edit, side, value] { edit ([side, value] (Clip& c) { clipops::setDeclickChoice (c, side, value); }); });
    };
    addChoice (tr ("Automatic"), clipops::declickAutomatic, choice == clipops::declickAutomatic || choice == clipops::declickJoined);
    addChoice (tr ("Disabled"), clipops::declickDisabled, choice == clipops::declickDisabled);
    addChoice (tr ("Enabled"), clipops::declickEnabled, choice == clipops::declickEnabled);
    menu.addSectionHeader (tr ("Curve shape"));
    const auto linear = (clip.linearFades & (side == 0 ? 1 : 2)) != 0;
    const auto setLinear = [edit, side] (bool on) {
        edit ([side, on] (Clip& c) {
            const auto bit = (std::uint8_t) (side == 0 ? 1 : 2);
            c.linearFades = on ? (std::uint8_t) (c.linearFades | bit) : (std::uint8_t) (c.linearFades & ~bit);
        });
    };
    menu.addItem (tr ("Smooth curve"), true, ! linear, [setLinear] { setLinear (false); });
    menu.addItem (tr ("Single curve"), true, linear, [setLinear] { setLinear (true); });
    return menu;
}

int PlaylistComponent::clipAt (int x, int y, bool& onRightEdge) const
{
    bool left = false;
    return clipAt (x, y, onRightEdge, left);
}

int PlaylistComponent::clipAt (int x, int y, bool& onRightEdge, bool& onLeftEdge) const
{
    const auto& playlist = engine.getSong().playlist;
    const auto track = yToTrack (y);
    onRightEdge = onLeftEdge = false;

    for (int i = (int) playlist.size() - 1; i >= 0; --i)
    {
        const auto& c = playlist[(std::size_t) i];

        if (c.track != track)
            continue;

        // the left edge only on clips wide enough to still be grabbed in the middle
        gesture::EdgeRules rules;
        rules.minimumForLeft = 24.0;
        // FL's Edit › "Allow resizing from left" off (FUN_00e8e840): the left edge is part of the body, it moves the clip
        rules.allowLeft = EditorGlobals::get().leftResizing;

        if (const auto zone = gesture::zoneAt (x, tickToX ((double) c.start), tickToX ((double) c.end()), rules); zone != gesture::Zone::outside)
        {
            onRightEdge = zone == gesture::Zone::rightEdge;
            onLeftEdge = zone == gesture::Zone::leftEdge;
            return i;
        }
    }

    return -1;
}

std::int64_t PlaylistComponent::snapLine (double tick) const
{
    // the grid line nearest to `tick` (FL's ruler click, round 169d); "Nearest" (clip edges) as snap() does
    const auto& option = grid::resolve (snapBox);
    if (option.kind == grid::Kind::nearest)
        return snap (tick, true);
    const auto step = gridTicks();
    if (step == (std::int64_t) ppq * juce::jmax (1, engine.getSong().beatsPerBar)) // (whole bars: time signatures)
    {
        const auto b = engine.getSong().barAt ((std::int64_t) std::max (0.0, std::floor (tick)));
        return tick - (double) b.tick >= (double) b.length * 0.5 ? b.tick + b.length : b.tick;
    }
    return noteedit::snapRound (tick, step);
}

void PlaylistComponent::seekTo (int x, bool snapOn)
{
    const auto tick = juce::jmax (0.0, snapOn ? (double) snapLine (xToTick (x)) : xToTick (x));

    if (engine.getSong().mode != PlayMode::song)
        engine.editSong ([] (Song& s) { s.mode = PlayMode::song; }, false);

    // while playing, FL's "Quantize song jumps" makes the jump wait for the next beat / bar
    const auto waits = engine.getTransport().playing.load() && engine.quantizeSongJumps.load() > 0;
    engine.jumpTo (tick);
    if (! waits)
        playheadTick = tick;
    repaint();
}

bool PlaylistComponent::isSelected (int index) const
{
    return std::binary_search (selection.begin(), selection.end(), index);
}

void PlaylistComponent::setSelection (std::vector<int> indices)
{
    std::sort (indices.begin(), indices.end());
    indices.erase (std::unique (indices.begin(), indices.end()), indices.end());
    selection = std::move (indices);
    knownClipCount = engine.getSong().playlist.size();
    repaint();
}

void PlaylistComponent::sortClips (std::vector<int> placed)
{
    auto tracked = selection;
    const auto selected = tracked.size();
    tracked.insert (tracked.end(), placed.begin(), placed.end());
    std::vector<int> conflicts;
    engine.editSong ([&tracked, &conflicts, selected] (Song& s) {
        noteedit::sortByStartTracking (s.playlist, tracked);
        // the placed audio clips on an audio track play through its mixer track (round 146a; issue #15: only those,
        // FL's way — a shared channel waits for the "Make clip unique?" answer)
        const std::vector<int> moved (tracked.begin() + (std::ptrdiff_t) selected, tracked.end());
        conflicts = trackmodes::audioTrackConflicts (s, moved);
        trackmodes::followAudioTracks (s, moved);
    }, false);
    placed.assign (tracked.begin() + (std::ptrdiff_t) selected, tracked.end());
    tracked.resize (selected);
    setSelection (tracked);
    if (! conflicts.empty())
        askToMakeClipsUnique (std::move (placed), std::move (conflicts));
}

void PlaylistComponent::askToMakeClipsUnique (std::vector<int> placed, std::vector<int> channels)
{
    // FL 2026 asks once per channel; Yes = a copy of the channel for the placed clips, routed to the audio track
    if (channels.empty())
        return;
    const auto channel = channels.front();
    channels.erase (channels.begin());
    const auto& song = engine.getSong();
    if (channel < 0 || channel >= (int) song.channels.size())
        return askToMakeClipsUnique (std::move (placed), std::move (channels));
    // what the clips were when asked: an edit in between (the box is not modal) must not hit other clips
    std::vector<std::tuple<int, int, std::int64_t>> was;
    for (const auto i : placed)
        if (i >= 0 && i < (int) song.playlist.size())
            was.emplace_back (i, song.playlist[(std::size_t) i].track, song.playlist[(std::size_t) i].start);
    const auto name = juce::String::fromUTF8 (song.channels[(std::size_t) channel].name.c_str());
    auto options = juce::MessageBoxOptions()
                       .withIconType (juce::MessageBoxIconType::QuestionIcon)
                       .withTitle (tr ("Make clip unique?"))
                       .withMessage (tr ("The clip \"{0}\" is also used on another audio track.\n\nDo you want to make this instance unique and then route it to this audio track?",
                                         { name }))
                       .withButton (tr ("Yes"))
                       .withButton (tr ("No"))
                       .withAssociatedComponent (this);
    juce::AlertWindow::showAsync (options, [safe = juce::Component::SafePointer<PlaylistComponent> (this), placed, channels, channel, was] (int result) mutable {
        if (safe == nullptr)
            return;
        if (result == 1)
        {
            std::vector<int> still;
            const auto& now = safe->engine.getSong().playlist;
            for (const auto& [i, track, start] : was)
                if (i < (int) now.size() && now[(std::size_t) i].track == track && now[(std::size_t) i].start == start
                    && now[(std::size_t) i].channel == channel)
                    still.push_back (i);
            if (! still.empty())
                safe->engine.editSong ([&still] (Song& s) { trackmodes::followAudioTracks (s, still, true); });
        }
        safe->askToMakeClipsUnique (std::move (placed), std::move (channels));
    });
}

// FL's Edit › "Create new clips with manual fades" (EECreateNewClipsWithManualFades, off by default): a new audio clip's
// fades start reset and flagged manual (FUN_010657a0, then FUN_01065830 / FUN_01065840 set bits 0 / 1 of its fade flags),
// so its edges use its own fade handles instead of the channel's declicker: with no fade they are sharp cuts (the clip
// voice skips the declicker on a manual side, round 189i).
static std::uint32_t newClipFadeFlags() noexcept
{
    return EditorGlobals::get().manualFades ? 3u : 0u;
}

Clip PlaylistComponent::makeBrushClip (int track, std::int64_t start) const
{
    // what is chosen in the Picker: the current pattern, an audio clip or an automation clip
    const auto& song = engine.getSong();
    Clip c;
    c.pattern = song.currentPattern;
    c.track = track;
    c.start = juce::jmax<std::int64_t> (0, start);
    c.length = song.activePattern().effectiveLength (song.beatsPerBar);

    if (brush.kind == ClipKind::audio && brush.index >= 0 && brush.index < (int) song.channels.size()
        && song.channels[(std::size_t) brush.index].instrument == InstrumentKind::audioClip)
    {
        c.kind = ClipKind::audio;
        c.channel = brush.index;
        c.pattern = 0;
        c.flFadeFlags = newClipFadeFlags();

        // audio length in ticks at the current BPM
        if (auto data = engine.getSampleLibrary().load (juce::String::fromUTF8 (song.channels[(std::size_t) brush.index].samplePath.c_str())))
            c.length = juce::jmax<std::int64_t> (ppq / 4, (std::int64_t) ((double) data->frames() / data->sampleRate * song.bpm / 60.0 * ppq));
    }
    else if (brush.kind == ClipKind::automation && brush.index >= 0 && brush.index < (int) song.automations.size())
    {
        c.kind = ClipKind::automation;
        c.automation = brush.index;
        c.pattern = 0;
        c.length = (std::int64_t) ppq * juce::jmax (1, song.beatsPerBar) * 4; // 4 bars
    }

    return c;
}

void PlaylistComponent::setTimeSelection (std::int64_t from, std::int64_t to)
{
    engine.editSong ([from, to] (Song& s) {
        s.loopStart = from;
        s.loopEnd = to;
        s.mode = PlayMode::song;
    }, false);

    setSelection (to > from ? noteedit::clipsInTime (engine.getSong().playlist, from, to) : std::vector<int> {});
}

void PlaylistComponent::setTimeSelectionForSnapshot (int fromBar, int toBar)
{
    const auto bar = (std::int64_t) ppq * juce::jmax (1, engine.getSong().beatsPerBar);
    setTimeSelection (fromBar * bar, toBar * bar);
}

void PlaylistComponent::setViewForSnapshot (double pixelsPerBar, int firstBar, int firstTrack)
{
    const auto bar = (double) ppq * juce::jmax (1, engine.getSong().beatsPerBar);
    pixelsPerTick = pixelsPerBar / bar;
    scrollTicks = firstBar * bar;
    scrollY = juce::jmax (0, firstTrack) * defaultRowHeight();
    updateScrollBars();
    repaint();
}

ui::Hint PlaylistComponent::hoverForSnapshot()
{
    const auto& playlist = engine.getSong().playlist;
    const auto grid = gridArea();

    // the last clip fully in view (the floating Channel Rack tends to cover the first ones)
    for (int i = (int) playlist.size() - 1; i >= 0; --i)
        if (const auto r = clipBounds (i); grid.contains (r) && r.getWidth() >= 40 && playlist[(std::size_t) i].kind == ClipKind::pattern)
        {
            const auto at = juce::Point<int> (r.getRight() - 2, r.getCentreY() + 4);
            updateHover (at, {});
            return hintAt (at);
        }

    return {};
}

void PlaylistComponent::mouseDown (const juce::MouseEvent& e)
{
    downMods = e.mods;
    startedOnEmpty = erasedSomething = holdExpired = false;
    dragCopies = false;
    hover = {};
    gliding = false;

    if (e.mods.isMiddleButtonDown() && e.x >= headerWidth) // middle-drag pans, in every tool
    {
        const auto onClips = gridArea().contains (e.getPosition());
        const auto onRuler = e.y >= rulerTop && e.y < headerHeight;
        if (onClips && flmods::ctrl (e.mods) && ! e.mods.isAltDown()) // FL: Ctrl+middle-drag = vertical zoom
        {
            drag = Drag::heightZoom;
            const auto track = yToTrack (e.y);
            heightZoomTrackY = trackToY (track); // the top of the track under the mouse stays where it is
            heightZoomStartHeight = juce::jmax (1, heightOf (track));
            trackZoomStart = trackZoom;
            heightZoomDrag.begin (e.y);
            e.source.enableUnboundedMouseMovement (true);
            return;
        }
        if (onRuler || (onClips && e.mods.isAltDown())) // FL: middle-drag on the ruler / Alt+middle-drag = time zoom
        {
            drag = Drag::timeZoom;
            const auto width = (double) juce::jmax (1, gridArea().getWidth());
            timeZoomDrag.begin (width / pixelsPerTick, scrollTicks + width * 0.5 / pixelsPerTick, e.x, e.y);
            e.source.enableUnboundedMouseMovement (true);
            return;
        }

        drag = Drag::pan;
        panStartTicks = scrollTicks;
        panStartY = scrollY;
        setMouseCursor (editorCursor (gesture::Cursor::pan));
        return;
    }

    // the vertical zoom handle: drag up = taller tracks
    if (trackZoomHandle().contains (e.getPosition()) && e.mods.isLeftButtonDown())
    {
        drag = Drag::trackZoom;
        trackZoomStart = trackZoom;
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        return;
    }

    // track header divider: drag resizes the track above it (Shift: every track)
    if (const auto divider = dividerAt (e.getPosition()); divider >= 0 && e.mods.isLeftButtonDown())
    {
        drag = Drag::trackResize;
        resizeTrack = divider;
        resizeAll = e.mods.isShiftDown();
        resizeStartHeight = heightOf (divider);
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        return;
    }

    // round 177b: automation lanes (their own gestures in every tool)
    if (laneMouseDown (e))
        return;

    // track header: the LED mutes (Ctrl+click or right-click on it: solo), a group header's arrow folds the
    // group, a click elsewhere selects the track; right-click elsewhere: track menu
    if (e.x < headerWidth && e.y >= tracksTop() && e.y < gridArea().getBottom())
    {
        const auto track = yToTrack (e.y);

        if (! flmods::popup (e.mods)) // round 177b: the lanes' fold arrow and "+A"
        {
            if (trackLaneArrow (track).expanded (2).contains (e.getPosition()))
            {
                editTrack (track, [] (PlaylistTrack& t) { t.lanesFolded = ! t.lanesFolded; });
                return;
            }
            if (trackAddLane (track).contains (e.getPosition()))
            {
                showAddLaneMenu (track);
                return;
            }
        }

        if (noteedit::isFoldedAway (engine.getSong(), track)) // a thin row of a folded group: only its menu
        {
            if (flmods::popup (e.mods))
                showTrackMenu (track);
            return;
        }

        const auto onLed = trackLed (track).expanded (3).contains (e.getPosition());
        const auto onArrow = trackArrow (track).contains (e.getPosition());

        if (trackAudition (track).expanded (2).contains (e.getPosition()))
        {
            if (! flmods::popup (e.mods)) // not an edit: no undo step, and nothing about it is saved
                engine.editSong ([track] (Song& s) { comp::setAudition (s, s.auditionLane == track ? -1 : track); }, false);
            repaint (0, headerHeight, headerWidth, getHeight() - headerHeight);
            return;
        }
        // the Simple headers (round 184a): M, S, volume, pan and "+FX" act on the track's mixer insert
        if (const auto insert = simpleInsert (track); insert > 0 && ! flmods::popup (e.mods))
        {
            const auto p = e.getPosition();
            if (onLed || trackSolo (track).contains (p))
            {
                const auto solo = ! onLed;
                engine.editSong ([insert, solo] (Song& s) {
                    auto& m = s.mixer[(std::size_t) insert];
                    if (solo)
                        m.solo = ! m.solo;
                    else
                        m.muted = ! m.muted;
                });
                return;
            }
            if (trackVolume (track).expanded (0, 4).contains (p) || trackPan (track).expanded (2).contains (p))
            {
                levelDrag = trackPan (track).expanded (2).contains (p) ? 2 : 1;
                levelInsert = insert;
                const auto& m = engine.getSong().mixer[(std::size_t) insert];
                levelStart = levelDrag == 2 ? m.pan : m.volume;
                if (levelDrag == 1) // a click on the bar sets the level there
                {
                    const auto bar = trackVolume (track);
                    const auto v = juce::jlimit (0.0f, 1.0f, (float) (p.x - bar.getX()) / (float) juce::jmax (1, bar.getWidth()));
                    levelStart = v;
                    engine.editLevels ([insert, v] (Song& s) { s.mixer[(std::size_t) insert].volume = v; });
                }
                return;
            }
            if (trackFx (track).contains (p))
            {
                if (onTrackEffects)
                    onTrackEffects (insert);
                return;
            }
        }

        // an audio track's buttons (FL 2026): [LMB] on the record button arms / disarms its mixer track, on the input
        // button opens the input source menu; a right-click on them does nothing in FL (no track menu either)
        const auto onArm = trackArm (track).expanded (2).contains (e.getPosition());
        const auto onInput = trackInput (track).expanded (2).contains (e.getPosition());
        if (onArm || onInput)
        {
            const auto insert = trackmodes::armableInsert (engine.getSong(), track);
            if (! flmods::popup (e.mods) && insert > 0)
            {
                if (onArm)
                    mixerinput::toggleArm (engine, insert, this);
                else
                    mixerinput::showMenu (engine, insert, this);
            }
            repaint (0, tracksTop(), headerWidth, getHeight() - tracksTop());
            return;
        }

        if (onLed && (flmods::popup (e.mods) || flmods::ctrl (e.mods)))
            engine.editSong ([track, count = numTracks()] (Song& s) {
                noteedit::soloTrack (s, track, count, &flsettings::trackSoloMemory(), flsettings::options().restoreStateAfterSolo.load());
            });
        else if (flmods::popup (e.mods))
            showTrackMenu (track);
        else if (onLed)
            editTrack (track, [] (PlaylistTrack& t) { t.muted = ! t.muted; });
        else if (onArrow)
            engine.editSong ([track] (Song& s) {
                const auto folded = track < (int) s.playlistTracks.size() && s.playlistTracks[(std::size_t) track].collapsed;
                noteedit::setGroupCollapsed (s, track, ! folded);
            });
        else if (! noteedit::isFoldedAway (engine.getSong(), track))
        {
            // Ctrl+click adds / removes a track, Shift+click selects the tracks from the last clicked one (round 165m:
            // FL's track menu settings, e.g. Performance settings, apply to every selected track)
            if (flmods::ctrl (e.mods))
            {
                if (selectedTracks.empty() && currentTrack >= 0)
                    selectedTracks.push_back (currentTrack);
                if (const auto it = std::find (selectedTracks.begin(), selectedTracks.end(), track); it != selectedTracks.end())
                    selectedTracks.erase (it);
                else
                    selectedTracks.push_back (track);
            }
            else if (e.mods.isShiftDown() && currentTrack >= 0)
            {
                selectedTracks.clear();
                for (int t = juce::jmin (currentTrack, track); t <= juce::jmax (currentTrack, track); ++t)
                    selectedTracks.push_back (t);
            }
            else
                selectedTracks = { track };
            currentTrack = track;
            if (simpleHeaders)
                selectTrackChannel (track); // (the typing piano and the touch keyboard play the track)
            repaint (0, tracksTop(), headerWidth, getHeight() - tracksTop());
        }
        return;
    }

    // ruler, FL 2026's rules (core/RulerGestures.h, docs/analise/REA_rodada168t.md): click = position; Ctrl / Shift /
    // double-click drag, the Select tool or a right-drag = time selection; Ctrl+right-drag or the Zoom tool = zoom to
    // a range; Shift+drag moves the selection. On a Mac, FL's Ctrl is Cmd (or Control, which is not a right click here)
    if (e.y >= rulerTop && e.y < headerHeight && e.x >= headerWidth)
    {
        const auto fl = flmods::of (e.mods);
        const auto& song = engine.getSong();
        const auto snapOn = ! e.mods.isAltDown();
        const auto tick = juce::jmax<std::int64_t> (0, snap (xToTick (e.x), snapOn));
        const auto markerHit = markerTagAt (e.x, e.y);

        ruler::Press press;
        press.mods = fl;
        press.clicks = e.getNumberOfClicks();
        press.tool = tools.getTool();
        press.onMarkerTag = markerHit >= 0;
        press.hasSelection = song.hasLoop();
        press.insideSelection = song.hasLoop() && xToTick (e.x) >= (double) song.loopStart && xToTick (e.x) < (double) song.loopEnd;

        loopAnchor = tick;
        rulerDoubleDrag = false;
        rulerMenuOnClick = false;

        switch (ruler::press (press))
        {
            case ruler::Action::markerMenu:
                showRulerMenu (e.x);
                return;

            case ruler::Action::zoomRange:
                drag = Drag::zoomRange;
                rubberBand = {};
                return;

            case ruler::Action::adjust:
                drag = Drag::loop;
                loopAnchor = ruler::adjustAnchor (song.loopStart, song.loopEnd, tick);
                setTimeSelection (juce::jmin (loopAnchor, tick), juce::jmax (loopAnchor, tick));
                return;

            case ruler::Action::select:
                drag = Drag::loop;
                // FL 2026 [SHOT round 166e]: a double click held and dragged draws a time selection, with the song
                // position at its start; released in place it clears it (as a Ctrl+click does)
                rulerDoubleDrag = press.clicks >= 2 && ! fl.right;
                // LYNX: a right click released in place with no selection opens the ruler menu (FL: does nothing)
                rulerMenuOnClick = fl.right;
                return;

            case ruler::Action::markerSpan:
            {
                const auto from = song.markers()[(std::size_t) markerHit].tick;
                auto to = juce::jmax (from, song.songLength());
                for (const auto& m : song.markers())
                    if (m.tick > from)
                        to = juce::jmin (to, m.tick);
                if (to > from)
                    setTimeSelection (from, to);
                return;
            }

            case ruler::Action::moveSelection:
                drag = Drag::timeShift;
                timeMoveFrom = song.loopStart;
                timeMoveTo = song.loopEnd;
                timeMoveAnchor = tick;
                return;

            case ruler::Action::moveWithClips:
                drag = Drag::timeMove;
                timeMoveOriginals = song.playlist;
                timeMoveFrom = song.loopStart;
                timeMoveTo = song.loopEnd;
                timeMoveAnchor = tick;
                timeMoveCopy = false;
                return;

            case ruler::Action::marker:
            {
                // a click on a marker's tag goes to the marker (LYNX), quantized while playing like any song jump
                const auto markerTick = song.markers()[(std::size_t) markerHit].tick;
                const auto waits = engine.getTransport().playing.load() && engine.quantizeSongJumps.load() > 0;
                engine.jumpToMarker (markerHit);
                if (! waits)
                    playheadTick = (double) markerTick;
                repaint();
                return;
            }

            case ruler::Action::seek:
                break;
        }

        drag = Drag::seek;
        seekTo (e.x, snapOn);
        EditorGlobals::get().seekMark = tick;
        return;
    }

    if (! gridArea().contains (e.getPosition()))
        return;

    bool rightEdge = false, leftEdge = false;
    const auto hit = clipAt (e.x, e.y, rightEdge, leftEdge);
    const auto tool = tools.getTool();

    // Performance mode (round 165m): while the song plays, a click on a clip of the performance zone launches it (the
    // button's release is the release of the Hold modes) and a right-click stops its track; Ctrl / Alt edit as usual
    if (const auto& song = engine.getSong(); hit >= 0 && song.mode == PlayMode::song && song.inPerformanceZone (song.playlist[(std::size_t) hit])
                                            && engine.getTransport().playing.load() && ! flmods::ctrl (e.mods) && ! e.mods.isAltDown())
    {
        const auto& clip = song.playlist[(std::size_t) hit];
        perf::Command c;
        c.track = clip.track;
        c.clipStart = clip.start;
        c.type = flmods::popup (e.mods) ? perf::Command::Type::stopTrack : perf::Command::Type::press;
        engine.performanceCommand (c);
        if (c.type == perf::Command::Type::press)
        {
            drag = Drag::perfLaunch;
            perfTrack = clip.track;
            perfClip = clip.start;
        }
        return;
    }

    // Ctrl+right-click on empty space: zoom to show everything; again: back to the previous view
    if (e.mods.isRightButtonDown() && flmods::ctrl (e.mods) && hit < 0)
    {
        if (zoomBeforeAll > 0.0)
        {
            pixelsPerTick = zoomBeforeAll;
            scrollTicks = scrollBeforeAll;
            zoomBeforeAll = 0.0;
            updateScrollBars();
            repaint();
        }
        else
        {
            zoomBeforeAll = pixelsPerTick;
            scrollBeforeAll = scrollTicks;
            zoomAll();
        }
        return;
    }

    // the menu button in a clip's title strip
    if (hit >= 0 && e.mods.isLeftButtonDown() && ! flmods::ctrl (e.mods) && clipMenuButton (hit).contains (e.getPosition()))
    {
        showClipMenu (hit);
        return;
    }

    // Zoom tool: right-click zooms out around the point (a click zooms in, a drag zooms to the range)
    if (tool == EditTool::zoom && e.mods.isRightButtonDown() && ! e.mods.isAltDown())
    {
        zoomOut (e.x);
        return;
    }

    // Alt+right-click on a clip: hear it while the button is held
    if (e.mods.isRightButtonDown() && e.mods.isAltDown() && hit >= 0)
    {
        startPreview (hit, e.x);
        return;
    }

    if (e.mods.isRightButtonDown() && hit >= 0 && gainHandle (hit).expanded (2.0f).contains (e.position))
    {
        showGainMenu (hit);
        return;
    }

    // FL's fade handle menu (PLFadeHandleMenu, round 189i)
    if (e.mods.isRightButtonDown() && hit >= 0)
        if (const auto fade = fadeHandleAt (hit, e.getPosition()); fade != 0)
        {
            showFadeHandleMenu (hit, fade - 1);
            return;
        }

    if (e.mods.isRightButtonDown())
    {
        // round 165, FL's in-Playlist automation editing: right-click on a point = its menu, on a tension handle =
        // straight segment, anywhere else in the curve's area = a new point there, dragged while the button is held
        // (Shift: on the curve, keeping the level); the title strip (or a clip too small to edit) still deletes
        if (const auto point = automationPointAt (hit, e.getPosition()); point >= 0)
        {
            showAutomationPointMenu (hit, point);
            return;
        }
        if (const auto handle = automationHandleAt (hit, e.getPosition()); handle >= 0)
        {
            const auto automation = engine.getSong().playlist[(std::size_t) hit].automation;
            automationPointMemory().tension = 0.0f; // FL's "default tension" also clears the editor's memory
            engine.editSong ([automation, handle] (Song& s) { setAutomationSegmentTension (s.automations[(std::size_t) automation].points, handle, 0.0f); });
            return;
        }
        if (automationEditable (hit) && automationBody (engine.getSong().playlist[(std::size_t) hit]).contains (e.position))
        {
            const auto& c = engine.getSong().playlist[(std::size_t) hit];
            const auto& automation = engine.getSong().automations[(std::size_t) c.automation];
            AutomationPoint p;
            p.tick = juce::jlimit<std::int64_t> (c.offset, c.offset + c.length, snap (xToTick (e.x), ! e.mods.isAltDown()) - c.start + c.offset);
            const auto body = automationBody (c);
            p.value = e.mods.isShiftDown() ? automation.valueAt ((double) p.tick)
                                           : juce::jlimit (0.0f, 1.0f, (body.getBottom() - (float) e.y) / juce::jmax (1.0f, body.getHeight()));
            int added = -1;
            // FL: the new point takes the mode and tension last used (round 190bw)
            engine.editSong ([a = c.automation, p, &added, m = automationPointMemory()] (Song& s) {
                added = insertAutomationPoint (s.automations[(std::size_t) a].points, p, m.mode, m.tension);
            });
            setSelection ({});
            drag = Drag::autoPoint;
            autoClip = hit;
            autoPoint = added;
            return;
        }

        // on the clip menu icon (top left): the clip menu; anywhere else on a clip: delete, as in FL
        // (press and hold on empty space: Knife)
        if (hit >= 0 && clipMenuButton (hit).contains (e.getPosition()))
        {
            showClipMenu (hit);
            return;
        }

        // FL 2026 [SHOT, round 165z-b]: a double right-click is the Mute tool while the button stays down. The first
        // click already deleted the clip under it and FL leaves it deleted; the sweep then mutes / unmutes every clip
        // it crosses, the first one deciding which (issue #11)
        const auto now = juce::Time::getMillisecondCounter();
        const auto doubleRight = gesture::isDoubleClick (lastRightClickTime, now, lastRightClickPos.getDistanceFrom (e.getPosition()));
        lastRightClickTime = doubleRight ? 0 : now;
        lastRightClickPos = e.getPosition();
        if (doubleRight)
        {
            drag = Drag::muting;
            touchedClips.clear();
            muteSweep = {};
            bool r = false, l = false;
            if (const auto clip = clipAt (e.x, e.y, r, l); clip >= 0)
            {
                touchedClips.push_back (clip);
                const auto m = muteSweep.touch (engine.getSong().playlist[(std::size_t) clip].muted);
                engine.editSong ([clip, m] (Song& s) { s.playlist[(std::size_t) clip].muted = m; });
            }
            return;
        }

        drag = Drag::erase;
        startedOnEmpty = hit < 0;
        setSelection ({}); // indices change while deleting

        if (hit >= 0)
        {
            erasedSomething = true;
            engine.editSong ([hit] (Song& s) { s.playlist.erase (s.playlist.begin() + hit); });
        }
        return;
    }

    // every tool: Ctrl+click on a clip adds/removes it from the selection; Ctrl+drag selects (Shift adds)
    if (hit >= 0 && flmods::ctrl (e.mods))
    {
        auto s = selection;

        if (isSelected (hit))
            s.erase (std::find (s.begin(), s.end(), hit));
        else
            s.push_back (hit);

        setSelection (s);
        return;
    }

    if ((hit < 0 && flmods::ctrl (e.mods)) || tool == EditTool::lasso
        || (hit < 0 && e.mods.isShiftDown() && tool != EditTool::brush))
    {
        drag = Drag::select;
        dragClip = hit; // a Lasso click without dragging selects this clip
        selectionBefore = e.mods.isShiftDown() ? selection : std::vector<int> {};
        rubberBand = {};
        setSelection (selectionBefore);
        return;
    }

    const auto snapOn = ! e.mods.isAltDown();

    auto grabClips = [&] (Drag kind) {
        if (! isSelected (hit))
            setSelection ({});

        const auto& playlist = engine.getSong().playlist;
        dragClip = hit;
        dragIndices = selection.empty() ? std::vector<int> { hit } : selection;
        dragOriginals.clear();

        for (auto i : dragIndices)
            dragOriginals.push_back (playlist[(std::size_t) i]);

        drag = kind;
        grabOffset = playlist[(std::size_t) hit].start - snap (xToTick (e.x), snapOn);
    };

    switch (tool)
    {
        case EditTool::eraser:
            drag = Drag::erase;
            setSelection ({});
            if (hit >= 0)
                engine.editSong ([hit] (Song& s) { s.playlist.erase (s.playlist.begin() + hit); });
            return;

        case EditTool::mute:
            drag = Drag::muting;
            touchedClips.clear();
            muteSweep = {};
            if (hit >= 0)
            {
                touchedClips.push_back (hit);
                const auto m = muteSweep.touch (engine.getSong().playlist[(std::size_t) hit].muted);
                engine.editSong ([hit, m] (Song& s) { s.playlist[(std::size_t) hit].muted = m; });
            }
            else
            {
                muteSweep.touch (false); // the Mute tool started on empty space mutes
            }
            return;

        case EditTool::knife:
            drag = Drag::knife;
            knifeFrom = knifeTo = e.getPosition();
            repaint();
            return;

        case EditTool::zoom:
            drag = Drag::zoomRange;
            rubberBand = {};
            return;

        case EditTool::listen:
            drag = Drag::listen;
            seekTo (e.x, ! e.mods.isAltDown());
            engine.play();
            return;

        case EditTool::slip:
            if (hit >= 0)
                grabClips (Drag::slip);
            return;

        case EditTool::pencil:
        case EditTool::brush:
        case EditTool::drumPaint:
        case EditTool::lasso:
            break;
    }

    // automation clip: drag a point, or click on the curve to add one (Pencil and Brush); round 165 (FL): Alt+click
    // deletes a point, the hollow handle between two points bends the segment
    if (hit >= 0 && ! e.mods.isShiftDown() && (tool == EditTool::pencil || tool == EditTool::brush))
    {
        auto point = automationPointAt (hit, e.getPosition());

        if (const auto handle = point < 0 ? automationHandleAt (hit, e.getPosition()) : -1; handle >= 0)
        {
            const auto& song = engine.getSong();
            const auto& points = song.automations[(std::size_t) song.playlist[(std::size_t) hit].automation].points;
            setSelection ({});
            drag = Drag::autoTension;
            autoClip = hit;
            autoPoint = handle;
            automationPointMemory() = { points[(std::size_t) handle].mode < flcurve::modeCount ? (int) points[(std::size_t) handle].mode : 0,
                                        points[(std::size_t) handle].tension };
            tensionStart = points[(std::size_t) handle].tension;
            // a positive tension lowers a rising segment's middle and raises a falling one's: the handle follows the mouse
            tensionSign = points[(std::size_t) handle + 1].value >= points[(std::size_t) handle].value ? -1.0f : 1.0f;
            return;
        }

        if (point < 0 && isOnAutomationCurve (hit, e.getPosition()))
        {
            const auto& c = engine.getSong().playlist[(std::size_t) hit];
            AutomationPoint p;
            p.tick = juce::jmax<std::int64_t> (0, snap (xToTick (e.x), snapOn) - c.start + c.offset);
            const auto body = automationBody (c);
            p.value = juce::jlimit (0.0f, 1.0f, (body.getBottom() - (float) e.y) / juce::jmax (1.0f, body.getHeight()));
            engine.editSong ([a = c.automation, p, m = automationPointMemory()] (Song& s) {
                insertAutomationPoint (s.automations[(std::size_t) a].points, p, m.mode, m.tension);
            });
            point = automationPointAt (hit, e.getPosition());
        }

        if (point >= 0)
        {
            setSelection ({});
            drag = Drag::autoPoint;
            autoClip = hit;
            autoPoint = point;
            return;
        }
    }

    // warp marker flags (round 175a): drag a marker sideways; the change is applied on release
    if (hit >= 0)
        if (const auto marker = warpMarkerAt (hit, e.getPosition()); marker > 0)
        {
            setSelection ({});
            warpClip = hit;
            warpIndex = marker;
            warpBeat = clipWarpMarkers (hit)[(std::size_t) marker].beats;
            drag = Drag::warpMarker;
            return;
        }

    // audio clip fade handles (drawn at the top corners of the body)
    if (hit >= 0)
        if (const auto fade = fadeHandleAt (hit, e.getPosition()); fade != 0)
        {
            setSelection ({});
            dragClip = hit;
            dragIndices = { hit };
            drag = fade == 1 ? Drag::fadeIn : Drag::fadeOut;
            return;
        }

    // FL 2026's gain handle (round 166e): drag up / down; on a selected clip, every selected audio clip follows
    if (hit >= 0 && gainHandle (hit).expanded (2.0f).contains (e.position))
    {
        const auto& song = engine.getSong();
        dragClip = hit;
        dragIndices.clear();
        gainStartDb.clear();
        for (auto i : isSelected (hit) ? selection : std::vector<int> { hit })
            if (i >= 0 && i < (int) song.playlist.size() && song.playlist[(std::size_t) i].kind == ClipKind::audio)
            {
                dragIndices.push_back (i);
                gainStartDb.push_back (clipops::clipGainToDb (song.playlist[(std::size_t) i].gain));
            }
        gainStartY = e.y;
        drag = Drag::clipGain;
        postDragHint();
        return;
    }

    // Alt+Shift+drag inside a clip: slip its content
    if (hit >= 0 && e.mods.isAltDown() && e.mods.isShiftDown() && ! rightEdge && ! leftEdge)
    {
        grabClips (Drag::slip);
        return;
    }

    // Shift+drag on a clip body drags a copy (on an edge, Shift trims like a plain edge drag)
    if (hit >= 0 && e.mods.isShiftDown() && ! rightEdge && ! leftEdge)
    {
        const auto sources = isSelected (hit) ? selection : std::vector<int> { hit };
        const auto grabbedAt = (std::size_t) (std::find (sources.begin(), sources.end(), hit) - sources.begin());
        std::vector<Clip> copies;
        std::vector<int> added;

        for (auto i : sources)
            copies.push_back (engine.getSong().playlist[(std::size_t) i]);

        // Shift+V held: the copies get their own pattern / automation (FL's "make unique" while cloning)
        const auto unique = juce::KeyPress::isKeyCurrentlyDown ('V') || juce::KeyPress::isKeyCurrentlyDown ('v');

        engine.editSong ([&] (Song& s) {
            for (const auto& c : copies)
            {
                added.push_back ((int) s.playlist.size());
                s.playlist.push_back (c);
            }

            if (unique)
                polish::makeClipsUnique (s, added);
        });

        if (unique)
            for (std::size_t j = 0; j < added.size(); ++j)
                copies[j] = engine.getSong().playlist[(std::size_t) added[j]];
        setSelection (added);
        dragIndices = added;
        dragOriginals = copies;
        dragCopies = true;
        dragClip = added[grabbedAt];
        drag = Drag::move;
        grabOffset = copies[grabbedAt].start - snap (xToTick (e.x), snapOn);
        return;
    }

    // a take lane's clip (round 177c): a drag on its body swipes that range to the comp, Logic's Quick Swipe with the normal
    // tool; its title still moves it and its edges still resize. Never on an ordinary group: only lanes of a comp track.
    // (comp::swipeStyle: which tools swipe on a drag)
    if (hit >= 0 && ! rightEdge && ! leftEdge && ! flmods::ctrl (e.mods) && comp::dragSwipes (comp::swipeStyle, tool == EditTool::brush))
    {
        const auto& song = engine.getSong();
        const auto& c = song.playlist[(std::size_t) hit];
        const auto clipTop = (float) trackToY (c.track);
        const auto title = clipHeaderHeight (juce::Rectangle<float> (0.0f, clipTop, 1.0f, (float) heightOf (c.track)));
        if (c.kind == ClipKind::audio && comp::isComp (song, comp::groupOf (song, c.track).comp) && comp::groupOf (song, c.track).isLane (c.track)
            && (float) e.y >= clipTop + title)
        {
            setSelection ({});
            drag = Drag::swipe;
            swipeLane = c.track;
            swipeAnchor = swipeEnd = snap (xToTick (e.x) + (snapOn ? gridTicks() / 2.0 : 0.0), snapOn);
            postDragHint();
            return;
        }
    }

    if (hit >= 0)
    {
        grabClips (rightEdge ? Drag::resize : leftEdge ? Drag::trimLeft : Drag::move);
        stretchOriginals.clear();
        if (EditorGlobals::get().stretchOnResize && (rightEdge || leftEdge))
            stretchOriginals = engine.getSong().playlist;

        if (drag == Drag::resize && ripple)
        {
            setSelection ({}); // a ripple resize works on the grabbed clip only
            dragIndices = { hit };
            dragOriginals = { engine.getSong().playlist[(std::size_t) hit] };
            rippleOriginals = engine.getSong().playlist;
        }

        const auto& c = engine.getSong().playlist[(std::size_t) hit];

        if (c.kind == ClipKind::pattern && c.pattern != engine.getSong().currentPattern)
            engine.editSong ([p = c.pattern] (Song& s) { s.currentPattern = p; }, false);
        return;
    }

    // paints what is chosen in the Picker (the current pattern, an audio clip or an automation clip)
    setSelection ({});
    // an instrument track places its own pattern (FL: named after the channel, made on the first click; round 151)
    if (const auto track = yToTrack (e.y); brush.kind == ClipKind::pattern && track >= 0 && track < (int) engine.getSong().playlistTracks.size()
                                           && engine.getSong().playlistTracks[(std::size_t) track].mode == PlaylistTrackMode::instrument)
    {
        engine.editSong ([track] (Song& s) {
            if (const auto p = trackmodes::instrumentTrackPattern (s, track); p >= 0)
                s.currentPattern = p;
        });
        engine.setSelectedChannel (engine.getSong().playlistTracks[(std::size_t) track].channel); // its piano roll is the channel's
    }

    auto c = makeBrushClip (yToTrack (e.y), snap (xToTick (e.x), snapOn));

    if (c.kind == ClipKind::pattern)
        brush = {};
    // a track locked to content places its own content (FL: the paint loop of FUN_00e5cd20 → FUN_01220fe0, round 168f)
    pltracks::applyLockToContent (engine.getSong(), c);

    engine.editSong ([c] (Song& s) {
        s.playlist.push_back (c);
        s.mode = PlayMode::song; // someone arranging wants to hear the song
    });
    dragClip = (int) engine.getSong().playlist.size() - 1;
    dragIndices = { dragClip };
    dragOriginals = { c };
    grabOffset = 0;

    if (tool == EditTool::brush)
    {
        drag = Drag::paint;
        paintClip = c;
        nextPaintTick = c.end();
        return;
    }

    drag = Drag::move;
}

void PlaylistComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (levelDrag != 0 && levelInsert > 0 && levelInsert < (int) engine.getSong().mixer.size())
    {
        // a Simple header's volume (along the bar) or pan (up / down, as a knob); Shift: fine
        const auto fine = e.mods.isShiftDown() ? 0.2f : 1.0f;
        const auto insert = levelInsert;
        if (levelDrag == 1)
        {
            const auto v = juce::jlimit (0.0f, 1.0f, levelStart + fine * (float) e.getDistanceFromDragStartX() / (float) juce::jmax (40, headerWidth - 122));
            engine.editLevels ([insert, v] (Song& s) { s.mixer[(std::size_t) insert].volume = v; });
        }
        else
        {
            const auto v = juce::jlimit (-1.0f, 1.0f, levelStart - fine * (float) e.getDistanceFromDragStartY() / 80.0f);
            engine.editLevels ([insert, v] (Song& s) { s.mixer[(std::size_t) insert].pan = v; });
        }
        repaint (0, tracksTop(), headerWidth, getHeight() - tracksTop());
        return;
    }

    if (drag == Drag::perfLaunch)
        return; // (a launch: nothing moves)

    if (drag == Drag::swipe)
    {
        const auto snapOn = ! e.mods.isAltDown();
        swipeEnd = juce::jmax<std::int64_t> (0, snap (xToTick (e.x) + (snapOn ? gridTicks() / 2.0 : 0.0), snapOn));
        postDragHint();
        repaint (0, trackToY (swipeLane), getWidth(), heightOf (swipeLane));
        return;
    }

    if (drag == Drag::trackZoom)
    {
        setTrackZoom (trackZoomStart * std::pow (2.0, -e.getDistanceFromDragStartY() / 80.0), tracksTop());
        return;
    }

    if (drag == Drag::heightZoom)
    {
        heightZoomDrag.move (e.y, flmods::ctrl (e.mods));
        const auto height = heightZoomDrag.height (heightZoomStartHeight, 8, 640);
        setTrackZoom (trackZoomStart * (double) height / (double) heightZoomStartHeight, heightZoomTrackY);
        return;
    }

    if (drag == Drag::timeZoom)
    {
        const auto width = (double) juce::jmax (1, gridArea().getWidth());
        timeZoomDrag.move (e.x, e.y, flmods::ctrl (e.mods));
        pixelsPerTick = juce::jlimit (4.0 / (4 * ppq), 800.0 / (4 * ppq), width / timeZoomDrag.span);
        scrollTicks = juce::jmax (0.0, timeZoomDrag.centre - width * 0.5 / pixelsPerTick);
        zoomBeforeAll = 0.0;
        updateScrollBars();
        repaint();
        ui::postHint ({ tr ("Zooming: drag up = closer, sideways = scroll  [Ctrl] zoom only"), {}, false, false });
        return;
    }

    if (drag == Drag::trackResize)
    {
        // the size shown, kept at 100% zoom (the vertical zoom scales every track)
        const auto height = polish::clampTrackHeight ((int) std::lround ((resizeStartHeight + e.getDistanceFromDragStartY()) / trackZoom));
        std::vector<int> tracks;
        if (resizeAll)
            for (int t = 0; t < numTracks(); ++t)
                tracks.push_back (t);
        else
            tracks.push_back (resizeTrack);

        if (heightOf (resizeTrack) != scaledHeight (height))
            setTrackHeights (tracks, height);

        ui::postHint ({ resizeAll ? tr ("Resizing every track") : tr ("Resizing the track"), juce::String (scaledHeight (height)) + " px", false, false });
        return;
    }

    if (drag == Drag::preview)
        return;

    const auto snapOn = ! e.mods.isAltDown();
    // after the step below, the hint bar says what the gesture is doing
    const struct PostAfter
    {
        PlaylistComponent& list;
        ~PostAfter() { list.postDragHint(); }
    } postAfter { *this };

    switch (drag)
    {
        case Drag::none:
        case Drag::trackResize:
        case Drag::trackZoom:
        case Drag::heightZoom:
        case Drag::timeZoom:
        case Drag::preview:
            return;

        case Drag::pan:
            scrollTicks = juce::jmax (0.0, panStartTicks - e.getDistanceFromDragStartX() / pixelsPerTick);
            scrollY = juce::jmax (0, panStartY - e.getDistanceFromDragStartY());
            updateScrollBars();
            repaint();
            return;

        case Drag::laneDraw:
            laneDrawTo (e);
            return;

        case Drag::lanePoint:
        {
            const auto a = laneAutomation (laneTrack, laneIndex);
            if (a < 0)
                return;
            const auto& points = engine.getSong().automations[(std::size_t) a].points;
            if (autoPoint < 0 || autoPoint >= (int) points.size())
                return;
            // between its neighbours; Shift pressed after the click keeps the time, Ctrl the value (as in clips)
            auto tick = juce::jmax<std::int64_t> (0, snap (xToTick (e.x), snapOn));
            const auto lo = autoPoint > 0 ? points[(std::size_t) autoPoint - 1].tick : 0;
            const auto hi = autoPoint + 1 < (int) points.size() ? points[(std::size_t) autoPoint + 1].tick : std::numeric_limits<std::int64_t>::max();
            tick = juce::jlimit (lo, hi, tick);
            const auto body = laneBody (laneTrack, laneIndex);
            auto value = laneValueOf (a, juce::jlimit (0.0f, 1.0f, (body.getBottom() - (float) e.y) / juce::jmax (1.0f, body.getHeight())));
            if (e.mods.isShiftDown() && ! downMods.isShiftDown())
                tick = points[(std::size_t) autoPoint].tick;
            if (flmods::ctrl (e.mods) && ! flmods::ctrl (downMods))
                value = points[(std::size_t) autoPoint].value;
            engine.editSong ([a, i = autoPoint, tick, value] (Song& s) {
                auto& p = s.automations[(std::size_t) a].points[(std::size_t) i];
                p.tick = tick;
                p.value = value;
            });
            return;
        }

        case Drag::laneTension:
        {
            const auto a = laneAutomation (laneTrack, laneIndex);
            if (a < 0 || autoPoint < 0 || autoPoint + 1 >= (int) engine.getSong().automations[(std::size_t) a].points.size())
                return;
            const auto range = juce::jmax (30.0f, laneBody (laneTrack, laneIndex).getHeight()) * (flmods::ctrl (e.mods) ? 4.0f : 1.0f);
            const auto up = -(float) e.getDistanceFromDragStartY() / range;
            const auto tension = juce::jlimit (-1.0f, 1.0f, tensionStart + tensionSign * up);
            engine.editSong ([a, i = autoPoint, tension] (Song& s) { setAutomationSegmentTension (s.automations[(std::size_t) a].points, i, tension); });
            return;
        }

        case Drag::autoPoint:
        {
            const auto& song = engine.getSong();
            if (autoClip < 0 || autoClip >= (int) song.playlist.size())
                return;
            const auto& c = song.playlist[(std::size_t) autoClip];
            if (c.automation < 0 || c.automation >= (int) song.automations.size())
                return;
            const auto& points = song.automations[(std::size_t) c.automation].points;
            if (autoPoint < 0 || autoPoint >= (int) points.size())
                return;

            // the point stays between its neighbours; Shift pressed after the click keeps the time, Ctrl the value
            auto tick = snap (xToTick (e.x), snapOn) - c.start + c.offset;
            const auto lo = autoPoint > 0 ? points[(std::size_t) autoPoint - 1].tick : 0;
            const auto hi = autoPoint + 1 < (int) points.size() ? points[(std::size_t) autoPoint + 1].tick : std::numeric_limits<std::int64_t>::max();
            tick = juce::jlimit (lo, hi, tick);
            const auto body = automationBody (c);
            auto value = juce::jlimit (0.0f, 1.0f, (body.getBottom() - (float) e.y) / juce::jmax (1.0f, body.getHeight()));

            if (e.mods.isShiftDown() && ! downMods.isShiftDown())
                tick = points[(std::size_t) autoPoint].tick;
            if (flmods::ctrl (e.mods) && ! flmods::ctrl (downMods))
                value = points[(std::size_t) autoPoint].value;

            engine.editSong ([a = c.automation, i = autoPoint, tick, value] (Song& s) {
                auto& p = s.automations[(std::size_t) a].points[(std::size_t) i];
                p.tick = tick;
                p.value = value;
            });
            return;
        }

        case Drag::autoTension:
        {
            const auto& song = engine.getSong();
            if (autoClip < 0 || autoClip >= (int) song.playlist.size())
                return;
            const auto a = song.playlist[(std::size_t) autoClip].automation;
            if (a < 0 || a >= (int) song.automations.size() || autoPoint < 0 || autoPoint + 1 >= (int) song.automations[(std::size_t) a].points.size())
                return;
            // the handle follows the mouse: a drag over the curve's height bends the segment fully (Ctrl: 4× finer)
            const auto range = juce::jmax (30.0f, automationBody (song.playlist[(std::size_t) autoClip]).getHeight()) * (flmods::ctrl (e.mods) ? 4.0f : 1.0f);
            const auto up = -(float) e.getDistanceFromDragStartY() / range;
            const auto tension = juce::jlimit (-1.0f, 1.0f, tensionStart + tensionSign * up);
            automationPointMemory().tension = tension; // (FL's editor memory: the next new point takes it)
            engine.editSong ([a, i = autoPoint, tension] (Song& s) { setAutomationSegmentTension (s.automations[(std::size_t) a].points, i, tension); });
            return;
        }

        case Drag::timeMove:
        {
            const auto here = juce::jmax<std::int64_t> (0, snap (xToTick (e.x), snapOn));
            const auto delta = juce::jmax (-timeMoveFrom, here - timeMoveAnchor);
            std::vector<int> moved;
            engine.editSong ([&] (Song& s) {
                s.playlist = timeMoveOriginals;
                moved = noteedit::moveTimeRange (s.playlist, timeMoveFrom, timeMoveTo, delta, timeMoveCopy);
                s.loopStart = timeMoveFrom + delta;
                s.loopEnd = timeMoveTo + delta;
            });
            setSelection (moved);
            return;
        }

        case Drag::timeShift: // FL: Shift+drag on the ruler moves the time selection (its length kept)
        {
            const auto here = juce::jmax<std::int64_t> (0, snap (xToTick (e.x), snapOn));
            const auto r = ruler::moved (timeMoveFrom, timeMoveTo, here - timeMoveAnchor);
            setTimeSelection (r.from, r.to);
            return;
        }

        case Drag::seek:
        case Drag::listen:
            seekTo (e.x, snapOn);
            return;

        case Drag::loop:
        {
            const auto here = juce::jmax<std::int64_t> (0, snap (xToTick (e.x) + (snapOn ? gridTicks() / 2.0 : 0.0), snapOn));
            setTimeSelection (juce::jmin (loopAnchor, here), juce::jmax (loopAnchor, here));
            return;
        }

        case Drag::clipGain:
        {
            const auto delta = (gainStartY - e.y) * clipops::clipGainDbPerPixel;
            std::vector<std::pair<int, float>> gains;
            for (std::size_t k = 0; k < dragIndices.size(); ++k)
                gains.push_back ({ dragIndices[k], clipops::clipGainFromDb (gainStartDb[k] + delta) });
            engine.editSong ([gains] (Song& s) {
                for (auto [i, g] : gains)
                    if (i < (int) s.playlist.size())
                        s.playlist[(std::size_t) i].gain = g;
            });
            postDragHint();
            repaint();
            return;
        }

        case Drag::warpMarker:
        {
            const auto markers = clipWarpMarkers (warpClip);
            if (warpIndex <= 0 || warpIndex >= (int) markers.size())
                return;
            const auto& clip = engine.getSong().playlist[(std::size_t) warpClip];
            const auto tick = snapOn ? (double) snap (xToTick (e.x) + gridTicks() / 2.0, true) : xToTick (e.x);
            const auto beats = (tick - warpedit::clipTickOfBeat (clip, 0.0)) / (ppq * std::clamp (clip.stretch, 0.01, 100.0));
            const auto lo = markers[(std::size_t) warpIndex - 1].beats + warpedit::minBeatGap;
            const auto hi = warpIndex + 1 < (int) markers.size() ? markers[(std::size_t) warpIndex + 1].beats - warpedit::minBeatGap : 1.0e6;
            warpBeat = juce::jlimit (lo, juce::jmax (lo, hi), beats);
            postDragHint();
            repaint();
            return;
        }

        case Drag::fadeIn:
        case Drag::fadeOut:
        {
            const auto& clip = engine.getSong().playlist[(std::size_t) dragClip];
            // FL's Edit › "Snap fade handles" (off by default, EESnapFadeHandles): off, the handles move freely
            const auto snapFade = snapOn && EditorGlobals::get().snapFadeHandles;
            const auto tick = snapFade ? snap (xToTick (e.x) + gridTicks() / 2.0, true) : (std::int64_t) xToTick (e.x);
            auto length = drag == Drag::fadeIn ? tick - clip.start : clip.end() - tick;
            length = juce::jlimit<std::int64_t> (0, clip.length, length);
            const auto in = drag == Drag::fadeIn;

            if ((in ? clip.fadeIn : clip.fadeOut) == std::min (length, clip.length - (in ? clip.fadeOut : clip.fadeIn)))
                return;

            engine.editSong ([i = dragClip, length, in] (Song& s) {
                auto& c = s.playlist[(std::size_t) i];
                (in ? c.fadeIn : c.fadeOut) = std::min (length, c.length - (in ? c.fadeOut : c.fadeIn));
            });
            return;
        }

        case Drag::select:
        case Drag::zoomRange:
        {
            if (e.getDistanceFromDragStart() < 3 && rubberBand.isEmpty())
                return;

            const auto area = gridArea();

            if (drag == Drag::zoomRange) // a time range over the whole height (also when dragged on the ruler)
            {
                const auto x0 = juce::jlimit (area.getX(), area.getRight(), juce::jmin (e.getMouseDownX(), e.x));
                const auto x1 = juce::jlimit (area.getX(), area.getRight(), juce::jmax (e.getMouseDownX(), e.x));
                rubberBand = juce::Rectangle<int>::leftTopRightBottom (x0, area.getY(), x1, area.getBottom());
                repaint();
                return;
            }

            rubberBand = juce::Rectangle<int> (e.getMouseDownPosition(), e.getPosition()).getIntersection (area);

            const auto t0 = xToTick (rubberBand.getX()), t1 = xToTick (rubberBand.getRight());
            const auto tr0 = yToTrack (rubberBand.getY()), tr1 = yToTrack (rubberBand.getBottom());
            const auto& playlist = engine.getSong().playlist;
            auto s = selectionBefore;

            for (int i = 0; i < (int) playlist.size(); ++i)
            {
                const auto& c = playlist[(std::size_t) i];

                if ((double) c.end() > t0 && (double) c.start < t1 && c.track >= tr0 && c.track <= tr1)
                    s.push_back (i);
            }

            setSelection (s);
            return;
        }

        case Drag::erase:
        {
            // right press-and-hold on empty space, then drag: a temporary Knife
            if (e.mods.isRightButtonDown() && startedOnEmpty && ! erasedSomething && ! holdExpired)
            {
                if (e.getLengthOfMousePress() >= 350)
                {
                    drag = Drag::knife;
                    knifeFrom = e.getMouseDownPosition();
                    knifeTo = e.getPosition();
                    repaint();
                    return;
                }

                if (e.getDistanceFromDragStart() > 3)
                    holdExpired = true;
            }

            bool edge = false;
            const auto hit = clipAt (e.x, e.y, edge);

            if (hit >= 0)
            {
                erasedSomething = true;
                engine.editSong ([hit] (Song& s) { s.playlist.erase (s.playlist.begin() + hit); });
            }
            return;
        }

        case Drag::muting:
        {
            bool edge = false;
            const auto hit = clipAt (e.x, e.y, edge);

            if (hit >= 0 && std::find (touchedClips.begin(), touchedClips.end(), hit) == touchedClips.end())
            {
                touchedClips.push_back (hit); // a double right-click started on empty space: the first clip decides
                const auto m = muteSweep.touch (engine.getSong().playlist[(std::size_t) hit].muted);
                engine.editSong ([hit, m] (Song& s) { s.playlist[(std::size_t) hit].muted = m; });
            }
            return;
        }

        case Drag::knife:
            knifeTo = e.getPosition();
            repaint();
            return;

        case Drag::paint:
        {
            // copies of the painted clip, end to end, on the track under the mouse
            const auto tick = snap (xToTick (e.x), snapOn);
            const auto length = juce::jmax<std::int64_t> (1, paintClip.length);

            for (int guard = 0; tick >= nextPaintTick && guard < 64; ++guard)
            {
                auto c = paintClip;
                c.start = nextPaintTick;
                c.track = yToTrack (e.y);
                nextPaintTick += length;
                if (noteedit::paintWouldStack (engine.getSong().playlist, c))
                    continue; // (FL: no copy over a clip of the same source, round 169d)
                engine.editSong ([c] (Song& s) { s.playlist.push_back (c); });
            }
            return;
        }

        case Drag::slip:
        {
            // the content moves by the (snapped) mouse movement; the clips stay put
            const auto delta = snap (xToTick (e.x), snapOn) - snap (xToTick (e.getMouseDownX()), snapOn);
            const auto grabbedAt = (std::size_t) (std::find (dragIndices.begin(), dragIndices.end(), dragClip) - dragIndices.begin());
            auto probe = dragOriginals[grabbedAt];
            noteedit::slip (probe, delta);

            if (engine.getSong().playlist[(std::size_t) dragClip].offset == probe.offset)
                return;

            engine.editSong ([this, delta] (Song& s) {
                for (std::size_t j = 0; j < dragIndices.size(); ++j)
                {
                    auto c = dragOriginals[j];
                    noteedit::slip (c, delta);
                    s.playlist[(std::size_t) dragIndices[j]].offset = c.offset;
                }
            });
            return;
        }

        case Drag::move:
        {
            if (dragOutToClipGrid (e)) // round 190ef
                return;
            const auto grabbedAt = (std::size_t) (std::find (dragIndices.begin(), dragIndices.end(), dragClip) - dragIndices.begin());
            const auto& grabbed = dragOriginals[grabbedAt];
            auto deltaTicks = juce::jmax<std::int64_t> (0, snap (xToTick (e.x), snapOn) + grabOffset) - grabbed.start;
            auto deltaTracks = yToTrack (e.y) - grabbed.track;

            // the group keeps its shape: clamp by the leftmost / topmost clip
            for (const auto& c : dragOriginals)
            {
                deltaTicks = juce::jmax (deltaTicks, -c.start);
                deltaTracks = juce::jlimit (-c.track, 499 - c.track, deltaTracks);
            }

            const auto& current = engine.getSong().playlist[(std::size_t) dragClip];

            if (current.start == grabbed.start + deltaTicks && current.track == grabbed.track + deltaTracks)
                return;

            engine.editSong ([this, deltaTicks, deltaTracks] (Song& s) {
                for (std::size_t j = 0; j < dragIndices.size(); ++j)
                {
                    auto& c = s.playlist[(std::size_t) dragIndices[j]];
                    c.start = dragOriginals[j].start + deltaTicks;
                    c.track = dragOriginals[j].track + deltaTracks;
                }
            });
            return;
        }

        case Drag::resize:
        {
            const auto grabbedAt = (std::size_t) (std::find (dragIndices.begin(), dragIndices.end(), dragClip) - dragIndices.begin());
            const auto& grabbed = dragOriginals[grabbedAt];
            const auto minimum = snapOn ? juce::jmax<std::int64_t> (10, gridTicks()) : (std::int64_t) 10;
            const auto end = snapOn ? snap (xToTick (e.x) + gridTicks() / 2.0, true) : (std::int64_t) xToTick (e.x);
            const auto delta = juce::jmax (minimum, end - grabbed.start) - grabbed.length;

            if (engine.getSong().playlist[(std::size_t) dragClip].length == grabbed.length + delta)
                return;

            // the STRETCH switch (FL's ClipStretchBtn, round 168f): an audio clip's audio stretches with its edge
            if (EditorGlobals::get().stretchOnResize && grabbed.kind == ClipKind::audio && ! stretchOriginals.empty())
            {
                engine.editSong ([this, delta, minimum] (Song& s) {
                    s.playlist = stretchOriginals;
                    for (std::size_t j = 0; j < dragIndices.size(); ++j)
                        if (const auto& o = dragOriginals[j]; o.kind == ClipKind::audio)
                            pltracks::stretchAudioClip (s, stretchOriginals, dragIndices[j], o.start, juce::jmax (minimum, o.length + delta),
                                                        EditorGlobals::get().createVariants);
                });
                return;
            }

            if (ripple && ! rippleOriginals.empty()) // the later clips on the track follow the end
            {
                engine.editSong ([this, length = juce::jmax (minimum, grabbed.length + delta)] (Song& s) {
                    s.playlist = rippleOriginals;
                    noteedit::rippleResize (s.playlist, dragClip, length);
                });
                return;
            }

            engine.editSong ([this, delta, minimum] (Song& s) {
                for (std::size_t j = 0; j < dragIndices.size(); ++j)
                {
                    auto& c = s.playlist[(std::size_t) dragIndices[j]];
                    c.length = juce::jmax (minimum, dragOriginals[j].length + delta);
                    c.fadeIn = juce::jmin (c.fadeIn, c.length);
                    c.fadeOut = juce::jmin (c.fadeOut, c.length - c.fadeIn);
                }
            });
            return;
        }

        case Drag::trimLeft:
        {
            // the start moves, the content stays where it is (the offset follows)
            const auto grabbedAt = (std::size_t) (std::find (dragIndices.begin(), dragIndices.end(), dragClip) - dragIndices.begin());
            const auto& grabbed = dragOriginals[grabbedAt];
            const auto minimum = snapOn ? juce::jmax<std::int64_t> (10, gridTicks()) : (std::int64_t) 10;
            const auto start = snapOn ? snap (xToTick (e.x) + gridTicks() / 2.0, true) : (std::int64_t) xToTick (e.x);
            const auto delta = start - grabbed.start;
            auto probe = grabbed;
            noteedit::trimStart (probe, grabbed.start + delta, minimum);

            if (engine.getSong().playlist[(std::size_t) dragClip].start == probe.start)
                return;

            if (EditorGlobals::get().stretchOnResize && grabbed.kind == ClipKind::audio && ! stretchOriginals.empty())
            {
                // stretching from the start: the end stays (round 168f)
                engine.editSong ([this, delta, minimum] (Song& s) {
                    s.playlist = stretchOriginals;
                    for (std::size_t j = 0; j < dragIndices.size(); ++j)
                        if (const auto& o = dragOriginals[j]; o.kind == ClipKind::audio)
                        {
                            const auto newStart = juce::jlimit<std::int64_t> (0, o.end() - minimum, o.start + delta);
                            pltracks::stretchAudioClip (s, stretchOriginals, dragIndices[j], newStart, o.end() - newStart,
                                                        EditorGlobals::get().createVariants);
                        }
                });
                return;
            }

            engine.editSong ([this, delta, minimum] (Song& s) {
                for (std::size_t j = 0; j < dragIndices.size(); ++j)
                {
                    auto c = dragOriginals[j];
                    noteedit::trimStart (c, c.start + delta, minimum);
                    s.playlist[(std::size_t) dragIndices[j]] = c;
                }
            });
            return;
        }
    }
}

void PlaylistComponent::postDragHint()
{
    const auto& song = engine.getSong();
    const auto count = juce::String ((int) std::max<std::size_t> (1, dragIndices.size()));
    auto position = [&song] (std::int64_t tick) {
        const auto bar = song.barAt (juce::jmax<std::int64_t> (0, tick));
        const auto beat = (tick - bar.tick) / ppq + 1;
        return juce::String (bar.number) + ":" + juce::String (beat);
    };
    const auto clip = dragClip >= 0 && dragClip < (int) song.playlist.size() ? &song.playlist[(std::size_t) dragClip] : nullptr;
    ui::Hint hint;

    switch (drag)
    {
        case Drag::move:
            if (clip == nullptr)
                return;
            hint.text = tr ("Moving {0} clip(s)  [Alt] no snap", { count });
            hint.value = position (clip->start) + "  " + tr ("Track {0}", { juce::String (clip->track + 1) });
            break;
        case Drag::resize:
        case Drag::trimLeft:
            if (clip == nullptr)
                return;
            hint.text = tr ("Resizing {0} clip(s)  [Alt] no snap", { count });
            hint.value = position (drag == Drag::resize ? clip->end() : clip->start);
            break;
        case Drag::slip: hint.text = tr ("Sliding the content inside the clip  [Alt] no snap"); break;
        case Drag::swipe: hint.text = tr ("Swiping this range to the comp  [Alt] no snap"); break;
        case Drag::clipGain:
            if (clip == nullptr)
                return;
            hint.text = tr ("Clip gain: drag up / down ({0} clip(s))", { count });
            hint.value = tr ("Gain: {0}", { gainText (clip->gain) });
            break;
        case Drag::fadeIn:
        case Drag::fadeOut: hint.text = EditorGlobals::get().snapFadeHandles ? tr ("Setting the fade  [Alt] no snap") : tr ("Setting the fade"); break;
        case Drag::warpMarker: hint.text = tr ("Moving the warp marker: the audio around it stretches  [Alt] no snap"); break;
        case Drag::autoPoint: hint.text = tr ("Moving the point: then [Shift] keeps time  [Ctrl] keeps value"); break;
        case Drag::autoTension: hint.text = tr ("Bending the segment: drag up / down  [Ctrl] fine"); break;
        case Drag::laneDraw:
            hint.text = drawFreehand ? tr ("Draw Mode: drawing freehand") : tr ("Draw Mode: drawing steps at the grid width  [Alt] freehand");
            break;
        case Drag::lanePoint:
        case Drag::laneTension:
        {
            const auto a = laneAutomation (laneTrack, laneIndex);
            if (a < 0 || autoPoint < 0 || autoPoint >= (int) song.automations[(std::size_t) a].points.size())
                return;
            const auto& automation = song.automations[(std::size_t) a];
            hint.text = drag == Drag::lanePoint ? tr ("Moving the point: then [Shift] keeps time  [Ctrl] keeps value")
                                                : tr ("Bending the segment: drag up / down  [Ctrl] fine");
            hint.value = automationPointValueText (automation.target, automation.points[(std::size_t) autoPoint].value) + "  "
                         + position (automation.points[(std::size_t) autoPoint].tick);
            break;
        }
        case Drag::select:
            hint.text = tr ("Selecting  [Shift] adds to the selection");
            hint.value = tr ("{0} selected", { juce::String ((int) selection.size()) });
            break;
        case Drag::loop:
            hint.text = tr ("Time selection  [Alt] no snap");
            hint.value = position (song.loopStart) + " - " + position (song.loopEnd);
            break;
        case Drag::erase: hint.text = tr ("Deleting what the mouse crosses"); break;
        case Drag::muting: hint.text = tr ("Muting / unmuting what the mouse crosses"); break;
        case Drag::knife: hint.text = tr ("Cutting along the line  [Alt] no snap"); break;
        case Drag::paint: hint.text = tr ("Painting clips  [Alt] no snap"); break;
        case Drag::timeMove: hint.text = tr ("Moving the time selection with its clips  [Alt] no snap"); break;
        case Drag::timeShift:
            hint.text = tr ("Moving the time selection  [Alt] no snap");
            hint.value = position (song.loopStart) + " - " + position (song.loopEnd);
            break;
        case Drag::seek:
            hint.text = tr ("Song position  [Alt] no snap");
            hint.value = position ((std::int64_t) playheadTick);
            break;
        case Drag::listen:
        case Drag::pan:
        case Drag::zoomRange:
        case Drag::trackResize:
        case Drag::trackZoom:
        case Drag::heightZoom:
        case Drag::timeZoom:
        case Drag::preview:
        case Drag::none: return;
    }

    ui::postHint (hint);
}

void PlaylistComponent::mouseUp (const juce::MouseEvent& e)
{
    if (drag == Drag::swipe)
    {
        drag = Drag::none;
        if (const auto from = juce::jmin (swipeAnchor, swipeEnd), to = juce::jmax (swipeAnchor, swipeEnd); to > from)
        {
            engine.editSong ([lane = swipeLane, from, to] (Song& s) { comp::swipe (s, lane, from, to); });
            sortClips();
        }
        swipeLane = -1;
        repaint();
        return;
    }

    ui::clearPostedHint();
    if (levelDrag != 0)
    {
        levelDrag = 0;
        levelInsert = -1;
        return;
    }
    if (drag == Drag::perfLaunch)
    {
        perf::Command c;
        c.type = perf::Command::Type::release;
        c.track = perfTrack;
        c.clipStart = perfClip;
        engine.performanceCommand (c);
        drag = Drag::none;
        perfTrack = -1;
        return;
    }
    if (drag == Drag::warpMarker)
    {
        // one undoable edit: the clip gets its own copy of the channel if others share it (warpedit), then the marker
        // moves (in the beats the channel plays, so the markers are rewritten as played first)
        const auto seconds = clipFileSeconds (warpClip);
        const auto markers = clipWarpMarkers (warpClip);
        if (seconds > 0.0 && warpIndex > 0 && warpIndex < (int) markers.size() && std::abs (markers[(std::size_t) warpIndex].beats - warpBeat) > 1.0e-9)
            engine.editSong ([clip = warpClip, index = warpIndex, beats = warpBeat, seconds] (Song& s) {
                const auto channel = warpedit::channelForClipEdit (s, clip);
                if (channel < 0)
                    return;
                auto& sampler = s.channels[(std::size_t) channel].sampler;
                sampler.warp = warpedit::playedMarkers (sampler, seconds);
                warpedit::moveMarker (sampler, index, beats, seconds);
            });
        drag = Drag::none;
        warpClip = warpIndex = -1;
        repaint();
        return;
    }
    if (drag == Drag::knife)
        cutWithKnife (knifeFrom, knifeTo, ! e.mods.isAltDown());

    if (drag == Drag::zoomRange)
    {
        if (rubberBand.getWidth() > 4)
            zoomToTicks (xToTick (rubberBand.getX()), xToTick (rubberBand.getRight()));
        else
            zoomIn (e.x); // a click zooms in around the point (right-click zooms out)
    }

    if (drag == Drag::select && rubberBand.isEmpty() && dragClip >= 0)
    {
        auto s = selectionBefore;
        if (auto it = std::find (s.begin(), s.end(), dragClip); it != s.end())
            s.erase (it);
        else
            s.push_back (dragClip);
        setSelection (s);
    }

    // Ctrl+click on the ruler without dragging clears the time selection (and so does a double click); a right click
    // in place keeps what it did (FL: moved the nearer edge) and, with no selection, opens LYNX's ruler menu
    if (drag == Drag::loop && e.getDistanceFromDragStart() < 3 && ! flmods::of (downMods).right)
        setTimeSelection (0, 0);
    else if (drag == Drag::loop && rulerMenuOnClick && ruler::isClick (e.getDistanceFromDragStartX(), e.getDistanceFromDragStartY()))
    {
        rulerMenuOnClick = false;
        showRulerMenu (e.getMouseDownX());
    }
    else if (drag == Drag::loop && rulerDoubleDrag && engine.getSong().hasLoop())
    {
        const auto start = (double) engine.getSong().loopStart;
        const auto waits = engine.getTransport().playing.load() && engine.quantizeSongJumps.load() > 0;
        engine.jumpTo (start);
        if (! waits)
            playheadTick = start;
    }
    rulerDoubleDrag = false;
    rulerMenuOnClick = false;

    if (drag == Drag::listen)
        engine.stop();

    if (drag == Drag::preview)
        stopPreview();

    resizeTrack = -1;

    if (drag == Drag::move)
    {
        // FL reroutes only the clips that landed on another track: sliding a clip
        // along its own audio track leaves its channel's insert alone
        std::vector<int> moved;
        const auto& playlist = engine.getSong().playlist;
        for (std::size_t j = 0; j < dragIndices.size() && j < dragOriginals.size(); ++j)
        {
            const auto i = dragIndices[j];
            if (i >= 0 && i < (int) playlist.size() && playlist[(std::size_t) i].track != dragOriginals[j].track)
                moved.push_back (i);
        }
        // round 190cg: the lane automation under a clip goes along its track with it (same-track moves and copies)
        if (automationFollows())
        {
            struct Carry
            {
                int track;
                std::int64_t from, to, delta;
            };
            std::vector<Carry> carries;
            for (std::size_t j = 0; j < dragIndices.size() && j < dragOriginals.size(); ++j)
            {
                const auto i = dragIndices[j];
                const auto& o = dragOriginals[j];
                if (i >= 0 && i < (int) playlist.size() && playlist[(std::size_t) i].track == o.track && playlist[(std::size_t) i].start != o.start
                    && o.kind != ClipKind::automation)
                    carries.push_back ({ o.track, o.start, o.end(), playlist[(std::size_t) i].start - o.start });
            }
            if (! carries.empty() && ! arrange::laneAutomations (engine.getSong()).empty())
                engine.editSong ([carries, copy = dragCopies] (Song& s) {
                    for (const auto& c : carries)
                        arrange::carryLaneAutomation (s, c.track, c.from, c.to, c.delta, copy);
                });
        }
        sortClips (moved);
        dragCopies = false;
    }
    else if (drag == Drag::paint)
    {
        std::vector<int> painted; // the first painted clip and the copies appended after it
        for (auto i = dragClip; i >= 0 && i < (int) engine.getSong().playlist.size(); ++i)
            painted.push_back (i);
        sortClips (painted);
    }
    else if (drag == Drag::resize || drag == Drag::trimLeft || drag == Drag::timeMove)
        sortClips();

    rippleOriginals.clear();
    timeMoveOriginals.clear();
    autoClip = autoPoint = -1;
    laneTrack = laneIndex = -1;

    drag = Drag::none;
    dragClip = -1;
    dragIndices.clear();
    dragOriginals.clear();
    touchedClips.clear();
    rubberBand = {};
    knownClipCount = engine.getSong().playlist.size();
    hover = {};
    updateHover (e.getPosition(), e.mods); // the cursor of what is under the mouse now
    repaint();
}

void PlaylistComponent::cutWithKnife (juce::Point<int> from, juce::Point<int> to, bool snapOn)
{
    const auto& playlist = engine.getSong().playlist;
    std::vector<std::pair<int, std::int64_t>> cuts;
    const auto round = snapOn ? gridTicks() / 2.0 : 0.0;

    if (from.getDistanceFrom (to) < 3)
    {
        bool edge = false;
        if (const auto hit = clipAt (from.x, from.y, edge); hit >= 0)
            cuts.push_back ({ hit, snap (xToTick (from.x) + round, snapOn) });
    }
    else
    {
        // every track the line crosses is cut where the line passes its middle
        const auto top = juce::jmin (from.y, to.y), bottom = juce::jmax (from.y, to.y);

        for (int i = 0; i < (int) playlist.size(); ++i)
        {
            const auto& c = playlist[(std::size_t) i];
            const auto trackHeight = heightOf (c.track);
            const auto rowY = trackToY (c.track) + trackHeight / 2;

            if (rowY < top - trackHeight / 2 || rowY > bottom + trackHeight / 2)
                continue;

            const auto y = juce::jlimit (top, bottom, rowY);
            const auto x = from.y == to.y ? (double) from.x : from.x + (double) (y - from.y) * (to.x - from.x) / (double) (to.y - from.y);
            const auto tick = snap (xToTick ((int) std::round (x)) + round, snapOn);

            if (tick > c.start && tick < c.end())
                cuts.push_back ({ i, tick });
        }
    }

    if (cuts.empty())
        return;

    setSelection ({});
    engine.editSong ([&cuts] (Song& s) {
        for (auto [index, tick] : cuts)
        {
            auto rest = splitClip (s.playlist[(std::size_t) index], tick);
            s.playlist.push_back (rest);
        }
    });
    sortClips();
}

void PlaylistComponent::zoomToTicks (double from, double to)
{
    if (to - from < 1.0)
        return;

    pixelsPerTick = juce::jlimit (4.0 / (4 * ppq), 800.0 / (4 * ppq), gridArea().getWidth() / (to - from));
    scrollTicks = juce::jmax (0.0, from);
    updateScrollBars();
    repaint();
}

void PlaylistComponent::zoomIn (int anchorX)
{
    const auto anchor = xToTick (anchorX);
    pixelsPerTick = juce::jlimit (4.0 / (4 * ppq), 800.0 / (4 * ppq), pixelsPerTick * 2.0);
    scrollTicks = juce::jmax (0.0, anchor - (anchorX - headerWidth) / pixelsPerTick);
    updateScrollBars();
    repaint();
}

void PlaylistComponent::zoomOut (int anchorX)
{
    const auto anchor = xToTick (anchorX);
    pixelsPerTick = juce::jlimit (4.0 / (4 * ppq), 800.0 / (4 * ppq), pixelsPerTick / 2.0);
    scrollTicks = juce::jmax (0.0, anchor - (anchorX - headerWidth) / pixelsPerTick);
    updateScrollBars();
    repaint();
}

void PlaylistComponent::insertTimeSelection()
{
    const auto& song = engine.getSong();

    if (! song.hasLoop())
        return;

    setSelection ({});
    engine.editSong ([from = song.loopStart, to = song.loopEnd] (Song& s) { noteedit::insertTime (s.playlist, s.markers(), from, to - from); });
    sortClips();
}

void PlaylistComponent::deleteTimeSelection()
{
    const auto& song = engine.getSong();

    if (! song.hasLoop())
        return;

    setSelection ({});
    engine.editSong ([from = song.loopStart, to = song.loopEnd] (Song& s) { noteedit::deleteTime (s.playlist, s.markers(), from, to); });
    sortClips();
}

// --- round 190cg (core/ArrangeOps.h) ---------------------------------------------------------------------------------
void PlaylistComponent::setLiveStyle (bool on)
{
    if (on == liveStyle)
        return;
    liveStyle = on;
    repaint();
}

bool PlaylistComponent::automationFollows() const noexcept
{
    const auto set = EditorGlobals::get().automationFollowsClips;
    return set < 0 ? liveStyle : set > 0;
}

void PlaylistComponent::setLoopSwitch (bool plays)
{
    if (engine.getSong().hasLoop())
        engine.editSong ([plays] (Song& s) { s.loopOff = ! plays; }, false);
    repaint();
}

void PlaylistComponent::timeCommand (int which)
{
    const auto& song = engine.getSong();
    const auto time = song.hasLoop();
    const auto from = song.loopStart, to = song.loopEnd;
    if (which == 3)
    {
        if (timeClipboard.length <= 0)
            return;
        setSelection ({});
        engine.editSong ([at = time ? from : snap ((double) songPositionTick(), true), clip = timeClipboard] (Song& s) { arrange::pasteTime (s, at, clip); });
    }
    else if (! time)
        return;
    else if (which == 2)
        timeClipboard = arrange::copyTime (song, from, to);
    else if (which == 4)
        engine.editSong ([from, to, clips = selection] (Song& s) { arrange::cropClips (s, clips, from, to); });
    else
    {
        setSelection ({});
        if (which == 1)
            timeClipboard = arrange::copyTime (song, from, to);
        engine.editSong ([from, to, which] (Song& s) {
            if (which == 0)
                arrange::duplicateTime (s, from, to);
            else
            {
                arrange::deleteTime (s, from, to);
                s.loopEnd = s.loopStart; // the range is gone
            }
        });
    }
    sortClips();
    repaint();
}

void PlaylistComponent::askTempoRange()
{
    const auto& song = engine.getSong();
    const auto set = song.tempoViewHigh > song.tempoViewLow;
    auto* box = new juce::AlertWindow (tr ("Tempo range shown"), tr ("Lowest and highest tempo of the tempo lane (BPM)"), juce::MessageBoxIconType::NoIcon, this);
    box->addTextEditor ("low", juce::String (set ? song.tempoViewLow : 60.0), tr ("Lowest"));
    box->addTextEditor ("high", juce::String (set ? song.tempoViewHigh : 200.0), tr ("Highest"));
    box->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    box->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    box->enterModalState (true, juce::ModalCallbackFunction::create ([this, box] (int result) {
        const auto low = juce::jlimit (minTempo, maxTempo, box->getTextEditorContents ("low").replaceCharacter (',', '.').getDoubleValue());
        const auto high = juce::jlimit (minTempo, maxTempo, box->getTextEditorContents ("high").replaceCharacter (',', '.').getDoubleValue());
        if (result == 1 && high > low + 1.0)
            engine.editSong ([low, high] (Song& s) { s.tempoViewLow = low; s.tempoViewHigh = high; });
        repaint();
    }), true);
}

void PlaylistComponent::sliceInsertTimeSelection()
{
    const auto& song = engine.getSong();

    if (! song.hasLoop())
        return;

    setSelection ({});
    engine.editSong ([from = song.loopStart, to = song.loopEnd] (Song& s) { noteedit::sliceInsertTime (s.playlist, s.markers(), from, to - from); });
    sortClips();
}

void PlaylistComponent::trimTimeSelection()
{
    const auto& song = engine.getSong();

    if (! song.hasLoop())
        return;

    setSelection ({});
    engine.editSong ([from = song.loopStart, to = song.loopEnd] (Song& s) {
        noteedit::trimTime (s.playlist, s.markers(), from, to);
        s.loopStart = s.loopEnd = 0; // (FL clears the time selection after a trim, FUN_00eb35f0)
    });
    sortClips();
}

void PlaylistComponent::selectAllWithTime()
{
    // FL's EESelAllAction (FUN_00eb1960): in the Playlist the time selection also covers the whole song, from 0 to the
    // end of the last clip rounded up to a whole bar (FUN_00e43d30), then every clip is selected (FUN_00e5a850)
    const auto& song = engine.getSong();
    std::vector<int> all ((std::size_t) song.playlist.size());
    std::int64_t end = 0;
    for (std::size_t i = 0; i < all.size(); ++i)
    {
        all[i] = (int) i;
        end = std::max (end, song.playlist[i].start + std::max<std::int64_t> (1, song.playlist[i].length));
    }
    if (end > 0)
    {
        const auto bar = song.barAt (end - 1);
        engine.editSong ([to = bar.tick + bar.length] (Song& s) {
            s.loopStart = 0;
            s.loopEnd = to;
            s.mode = PlayMode::song;
        }, false);
    }
    setSelection (all);
}

void PlaylistComponent::shiftSelectedClips (int steps, int tracks, bool fine)
{
    // the selected clips, else all of them (FL's menu offers these with nothing selected: FL's usual "selected, else all",
    // as Mute does, FUN_01061e80; the manual: "If no selection is made then all the Clips will be moved")
    auto chosen = selection;
    if (chosen.empty())
        for (int i = 0; i < (int) engine.getSong().playlist.size(); ++i)
            chosen.push_back (i);

    // FL (FUN_00e51730, round 190ag): one snap step, or with Alt held one pixel's worth of ticks at this zoom
    const auto step = fine ? clipops::pixelStepTicks (pixelsPerTick) : juce::jmax<std::int64_t> (1, gridTicks());
    moveClips (std::move (chosen), (std::int64_t) steps * step, tracks, true, true);
}

void PlaylistComponent::moveClips (std::vector<int> chosen, std::int64_t delta, int tracks, bool newUndoStep, bool keepInView)
{
    // the one move behind Shift+arrows, Alt+arrows and the Shift+Alt+wheel nudge (issue #147): the clips move together and
    // stop together at the song start and at the first / last track; nothing is recorded when they cannot move
    {
        auto probe = engine.getSong().playlist;
        const auto time = delta != 0 ? clipops::shiftClipsInTime (probe, chosen, delta) : 0;
        const auto rows = tracks != 0 ? clipops::shiftClipsTrack (probe, chosen, tracks) : 0;
        if (chosen.empty() || (time == 0 && rows == 0))
            return;
    }

    auto edge = delta < 0 ? std::numeric_limits<std::int64_t>::max() : std::numeric_limits<std::int64_t>::min();
    engine.editSong ([&chosen, &edge, delta, tracks] (Song& s) {
        if (delta != 0)
            clipops::shiftClipsInTime (s.playlist, chosen, delta);
        if (tracks != 0)
            clipops::shiftClipsTrack (s.playlist, chosen, tracks);
        for (auto i : chosen)
            if (i >= 0 && i < (int) s.playlist.size())
                edge = delta < 0 ? std::min (edge, s.playlist[(std::size_t) i].start) : std::max (edge, s.playlist[(std::size_t) i].end());
    }, newUndoStep);
    // (only a change of track can put an audio clip on another audio track: a move in time asks nothing)
    sortClips (tracks != 0 ? std::move (chosen) : std::vector<int> {});

    if (keepInView && delta != 0) // FL scrolls to keep the leading edge of the moved clips in view (FUN_00e7e290)
        if (const auto to = clipops::scrollToShowEdge (scrollTicks, (double) gridArea().getWidth(), pixelsPerTick, edge, delta > 0);
            ! juce::approximatelyEqual (to, scrollTicks))
        {
            gliding = false;
            scrollTicks = to;
            updateScrollBars();
            repaint();
        }
}

void PlaylistComponent::selectBySelectedSource()
{
    const auto found = clipops::clipsOfSource (engine.getSong(), makeBrushClip (0, 0));
    if (found.empty())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, tr ("No clips selected"),
                                                tr ("There are no clips associated with the currently selected source clip."));
        return;
    }
    setSelection (found);
}

std::unique_ptr<juce::AlertWindow> PlaylistComponent::makeHeaderDropDialog()
{
    // FL's "Dropped sample(s)" box (FUN_00e71430: the three choices and Cancel, in FL's order)
    auto box = std::make_unique<juce::AlertWindow> (tr ("Dropped sample(s)"), tr ("Do you want to create audio clip(s), audio track(s) or instrument track(s)?"),
                                                    juce::MessageBoxIconType::QuestionIcon);
    box->addButton (tr ("Audio clip(s)"), 1);
    box->addButton (tr ("Audio track(s)"), 2);
    box->addButton (tr ("Instrument track(s)"), 3);
    box->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    return box;
}

void PlaylistComponent::dropOnTrackHeader (const juce::StringArray& files, int track)
{
    juce::StringArray samples;
    for (const auto& f : files)
        if (SampleLibrary::isSampleFile (juce::File (f)))
            samples.add (f);
    if (samples.isEmpty())
        return;

    const auto mode = trackmodes::headerDropOptions().mode;
    if (mode != trackmodes::HeaderDrop::ask)
    {
        dropOnTrackHeaderAs (samples, track, mode);
        return;
    }

    // FL's question (FUN_00e71430, asked once for all the dropped files)
    auto* box = makeHeaderDropDialog().release();
    box->enterModalState (true, juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<PlaylistComponent> (this), samples, track] (int result) {
        if (safe == nullptr || result < 1 || result > 3)
            return;
        const auto how = result == 1 ? trackmodes::HeaderDrop::audioClips
                       : result == 2 ? trackmodes::HeaderDrop::audioTracks
                                     : trackmodes::HeaderDrop::instrumentTracks;
        safe->dropOnTrackHeaderAs (samples, track, how);
    }), true);
}

void PlaylistComponent::dropOnTrackHeaderAs (const juce::StringArray& files, int track, trackmodes::HeaderDrop how)
{
    const auto lock = trackmodes::headerDropOptions().lockToContent;
    std::vector<int> tracks;
    auto failed = false;
    for (const auto& f : files)
    {
        const juce::File file (f);
        const auto data = engine.getSampleLibrary().load (file.getFullPathName());
        if (data == nullptr)
            continue;
        const auto seconds = (double) data->frames() / data->sampleRate;
        const auto length = juce::jmax<std::int64_t> (ppq / 4, (std::int64_t) (seconds * engine.getSong().bpm / 60.0 * ppq));
        const auto path = file.getFullPathName().toStdString();
        const auto name = file.getFileNameWithoutExtension().toStdString();
        auto done = false;
        engine.editSong ([&] (Song& s) {
            const auto before = s.playlist.size();
            done = trackmodes::dropOnHeader (s, track, path, name, length, how, lock);
            for (auto i = before; done && i < s.playlist.size(); ++i)
                if (s.playlist[i].kind == ClipKind::audio)
                    s.playlist[i].flFadeFlags |= newClipFadeFlags();
        });
        if (done)
            tracks.push_back (track);
        else if (how != trackmodes::HeaderDrop::audioClips)
            failed = true;
        ++track; // (FL: each further file on the next track)
    }
    sortClips();
    if (! tracks.empty() && trackmodes::headerDropOptions().selectTracks)
    {
        currentTrack = tracks.front();
        selectedTracks = tracks;
        repaint();
    }
    if (failed)
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Can't create instrument/audio track"),
                                                tr ("No available mixer track was found."));
}

juce::PopupMenu PlaylistComponent::editMenu()
{
    auto key = [] (const char* id) { return shortcutText != nullptr ? shortcutText (id) : juce::String(); };
    const auto& song = engine.getSong();
    const auto any = ! selection.empty(), time = song.hasLoop();
    const auto allAudio = any && std::all_of (selection.begin(), selection.end(), [&song] (int i) {
        return i >= 0 && i < (int) song.playlist.size() && song.playlist[(std::size_t) i].kind == ClipKind::audio;
    });
    juce::PopupMenu m;
    m.addItem (tr ("Cut") + "\tCtrl+X", any, false, [this] { copySelection(); deleteSelection(); });
    m.addItem (tr ("Copy") + "\tCtrl+C", any, false, [this] { copySelection(); });
    m.addItem (tr ("Paste") + "\tCtrl+V", ! clipboard.empty(), false, [this] { pasteClips(); });
    m.addItem (tr ("Duplicate") + "\tCtrl+B", any || time, false, [this] { duplicateSelection(); });
    m.addItem (tr ("Delete") + "\tDel", any, false, [this] { if (ripple) rippleDeleteSelection(); else deleteSelection(); });
    m.addSeparator();
    m.addItem (tr ("Shift left") + key ("playlist.shiftLeft"), [this] { runAction ("playlist.shiftLeft"); });
    m.addItem (tr ("Shift right") + key ("playlist.shiftRight"), [this] { runAction ("playlist.shiftRight"); });
    m.addItem (tr ("Shift up") + key ("playlist.shiftUp"), [this] { runAction ("playlist.shiftUp"); });
    m.addItem (tr ("Shift down") + key ("playlist.shiftDown"), [this] { runAction ("playlist.shiftDown"); });
    m.addItem (tr ("Make unique") + key ("playlist.makeUnique"), any, false, [this] { runAction ("playlist.makeUnique"); });
    // (FL 2026: on with audio clips selected, off when the selection also holds other clips)
    m.addItem (tr ("Make unique as sample") + key ("playlist.makeUniqueAsSample"), allAudio, false, [this] { runAction ("playlist.makeUniqueAsSample"); });
    m.addSeparator();
    {
        auto& g = EditorGlobals::get();
        m.addItem (tr ("Allow resizing from left") + key ("playlist.leftResizing"), true, g.leftResizing, [this] { runAction ("playlist.leftResizing"); });
        m.addItem (tr ("Nudge clips with mouse wheel") + key ("playlist.wheelNudging"), true, g.wheelNudging, [this] { runAction ("playlist.wheelNudging"); });
    }
    {
        auto& o = trackmodes::headerDropOptions();
        m.addItem (tr ("Dropping on track header locks to content"), true, o.lockToContent, [this] {
            trackmodes::headerDropOptions().lockToContent = ! trackmodes::headerDropOptions().lockToContent;
            if (EditorGlobals::get().onOptionsChanged)
                EditorGlobals::get().onOptionsChanged();
        });
        juce::PopupMenu drop;
        using trackmodes::HeaderDrop;
        for (auto [how, text] : { std::pair { HeaderDrop::audioClips, TR_NOOP ("As audio clips") }, { HeaderDrop::audioTracks, TR_NOOP ("As audio tracks") },
                                  { HeaderDrop::instrumentTracks, TR_NOOP ("As instrument tracks") }, { HeaderDrop::ask, TR_NOOP ("Always ask") } })
        {
            juce::PopupMenu::Item item (tr (text));
            item.setTicked (o.mode == how).setAction ([this, how = how] {
                trackmodes::headerDropOptions().mode = how;
                if (EditorGlobals::get().onOptionsChanged)
                    EditorGlobals::get().onOptionsChanged();
            });
            item.isEnabled = true;
            drop.addItem (item);
        }
        m.addSubMenu (tr ("Drop audio on track headers"), drop);
    }
    m.addSeparator();
    m.addItem (tr ("Mute") + key ("playlist.muteClips"), [this] { runAction ("playlist.muteClips"); });
    m.addItem (tr ("Unmute") + key ("playlist.unmuteClips"), [this] { runAction ("playlist.unmuteClips"); });
    m.addSeparator();
    m.addItem (tr ("Insert space") + "\tCtrl+Ins", time, false, [this] { insertTimeSelection(); });
    m.addItem (tr ("Slice & insert space") + "\tCtrl+Alt+Ins", time, false, [this] { runAction ("playlist.sliceInsertSpace"); });
    m.addItem (tr ("Delete space") + "\tCtrl+Del", time, false, [this] { deleteTimeSelection(); });
    m.addItem (tr ("Trim selection") + key ("playlist.trimSpace"), time, false, [this] { runAction ("playlist.trimSpace"); });
    // round 190cg: Live's time commands and loop switch on the same time selection [live-exe-strings.txt:1645342…1645365
    // sMenuTextCutTime / CopyTime / sMenuPasteTime / DuplicateTime; 1645321 sMenuActivateLoop; 1636009 sMenuTextCrop]
    m.addItem (tr ("Duplicate time") + key ("playlist.duplicateTime"), time, false, [this] { timeCommand (0); });
    m.addItem (tr ("Cut time") + key ("playlist.cutTime"), time, false, [this] { timeCommand (1); });
    m.addItem (tr ("Copy time") + key ("playlist.copyTime"), time, false, [this] { timeCommand (2); });
    m.addItem (tr ("Paste time") + key ("playlist.pasteTime"), timeClipboard.length > 0, false, [this] { timeCommand (3); });
    m.addItem (tr ("Crop clips to the time selection") + key ("playlist.cropClips"), time && any, false, [this] { timeCommand (4); });
    m.addSeparator();
    m.addItem (tr ("Loop the time selection") + key ("playlist.loopSwitch"), time, song.loopPlays(), [this] { runAction ("playlist.loopSwitch"); });
    m.addItem (tr ("Halve the time selection") + key ("playlist.loopHalve"), time, false, [this] { runAction ("playlist.loopHalve"); });
    m.addItem (tr ("Double the time selection") + key ("playlist.loopDouble"), time, false, [this] { runAction ("playlist.loopDouble"); });
    m.addSeparator();
    m.addItem (tr ("Automation follows clips") + key ("playlist.automationFollowsClips"), true, automationFollows(),
               [this] { runAction ("playlist.automationFollowsClips"); });
    m.addSeparator();
    m.addItem (tr ("Merge selected clips") + key ("playlist.mergePatternClips"), [this] { runAction ("playlist.mergePatternClips"); });
    m.addItem (tr ("Merge similar pattern clips") + key ("playlist.mergeSimilarClips"), [this] { runAction ("playlist.mergeSimilarClips"); });
    m.addItem (tr ("Merge automation clips") + key ("playlist.mergeAutomationClips"), [this] { runAction ("playlist.mergeAutomationClips"); });
    m.addItem (tr ("Purge unused audio clips"), [this] { runAction ("playlist.purgeUnusedAudio"); });
    m.addSeparator();
    m.addItem (tr ("Pitch selected audio clips one semitone down") + key ("playlist.pitchClipsDown"), [this] { runAction ("playlist.pitchClipsDown"); });
    m.addItem (tr ("Pitch selected audio clips one semitone up") + key ("playlist.pitchClipsUp"), [this] { runAction ("playlist.pitchClipsUp"); });
    m.addItem (tr ("Reverse selected audio clips") + key ("playlist.reverseClips"), [this] { runAction ("playlist.reverseClips"); });
    m.addSeparator();
    m.addItem (tr ("Automatic crossfades (current project)"), true, song.autoCrossfades, [this] { runAction ("playlist.autoCrossfades"); });
    m.addItem (tr ("Create new clips with manual fades") + key ("playlist.manualFades"), true, EditorGlobals::get().manualFades,
               [this] { runAction ("playlist.manualFades"); });
    m.addItem (tr ("Snap fade handles") + key ("playlist.snapFadeHandles"), true, EditorGlobals::get().snapFadeHandles,
               [this] { runAction ("playlist.snapFadeHandles"); });
    m.addItem (tr ("Create variants when stretching audio clips"), true, EditorGlobals::get().createVariants, [this] { runAction ("playlist.createVariants"); });
    m.addItem (tr ("Select tracks when samples are dropped"), true, trackmodes::headerDropOptions().selectTracks, [] {
        trackmodes::headerDropOptions().selectTracks = ! trackmodes::headerDropOptions().selectTracks;
        if (EditorGlobals::get().onOptionsChanged)
            EditorGlobals::get().onOptionsChanged();
    });
    return m;
}

juce::PopupMenu PlaylistComponent::selectMenu()
{
    auto key = [] (const char* id) { return shortcutText != nullptr ? shortcutText (id) : juce::String(); };
    const auto& song = engine.getSong();
    juce::PopupMenu select;
    select.addItem (tr ("Deselect") + key ("playlist.deselect"), ! selection.empty() || song.hasLoop(), false, [this] { runAction ("playlist.deselect"); });
    select.addItem (tr ("Select all") + "\tCtrl+A", [this] { selectAllWithTime(); });
    select.addItem (tr ("Select by selected source") + key ("playlist.selectBySource"), [this] { runAction ("playlist.selectBySource"); });
    // (the counts are LYNX's own, round 153)
    const auto overlapping = clipops::overlappingClips (song), stacked = clipops::stackedClips (song), muted = clipops::mutedClips (song);
    select.addItem (tr ("Muted clips ({0})", { juce::String ((int) muted.size()) }), ! muted.empty(), false, [this, muted] { setSelection (muted); });
    select.addItem (tr ("Overlapping clips ({0})", { juce::String ((int) overlapping.size()) }), ! overlapping.empty(), false,
                    [this, overlapping] { setSelection (overlapping); });
    select.addItem (tr ("Stacked clips: hidden duplicates ({0})", { juce::String ((int) stacked.size()) }), ! stacked.empty(), false,
                    [this, stacked] { setSelection (stacked); });
    select.addItem (tr ("Invert selection") + key ("playlist.invertSelection"), [this] { runAction ("playlist.invertSelection"); });
    select.addSeparator();
    select.addItem (tr ("Select time around selection") + key ("playlist.selectTimeAround"), ! selection.empty(), false,
                    [this] { runAction ("playlist.selectTimeAround"); });
    select.addItem (tr ("Select previous time") + key ("playlist.timePrevious"), song.hasLoop(), false, [this] { runAction ("playlist.timePrevious"); });
    select.addItem (tr ("Select next time") + key ("playlist.timeNext"), song.hasLoop(), false, [this] { runAction ("playlist.timeNext"); });
    select.addSeparator();
    select.addItem (tr ("Magic lasso") + key ("playlist.magicLasso"), true, EditorGlobals::get().magicLasso, [this] { runAction ("playlist.magicLasso"); });
    return select;
}

juce::Rectangle<int> PlaylistComponent::trackLed (int track) const
{
    const auto h = heightOf (track);
    if (simpleHeaders) // the M button, in the first row (round 184a)
        return { headerWidth - 28 - meterColumn, trackToY (track) + juce::jmin (5, juce::jmax (0, h / 2 - 8)), 22, juce::jmin (15, h - 2) };
    return { headerWidth - 17 - meterColumn, trackToY (track) + h / 2 - 6, 12, 12 };
}

int PlaylistComponent::simpleInsert (int track) const
{
    return simpleHeaders ? simpletracks::insertOf (engine.getSong(), track) : -1;
}

juce::Rectangle<int> PlaylistComponent::trackSolo (int track) const
{
    if (simpleInsert (track) < 0 || noteedit::isFoldedAway (engine.getSong(), track))
        return {};
    return trackLed (track).translated (-25, 0);
}

juce::Rectangle<int> PlaylistComponent::trackVolume (int track) const
{
    const auto h = heightOf (track);
    if (simpleInsert (track) < 0 || h < simpleTwoRows || noteedit::isFoldedAway (engine.getSong(), track))
        return {};
    return { 12, trackToY (track) + h - 20, headerWidth - 122, 12 };
}

juce::Rectangle<int> PlaylistComponent::trackPan (int track) const
{
    const auto v = trackVolume (track);
    return v.isEmpty() ? juce::Rectangle<int>() : juce::Rectangle<int> (v.getRight() + 10, v.getCentreY() - 8, 16, 16);
}

juce::Rectangle<int> PlaylistComponent::trackFx (int track) const
{
    const auto v = trackVolume (track);
    return v.isEmpty() ? juce::Rectangle<int>() : juce::Rectangle<int> (v.getRight() + 34, v.getCentreY() - 8, 44, 16);
}

void PlaylistComponent::setSimpleHeaders (bool on)
{
    if (on == simpleHeaders)
        return;
    simpleHeaders = on;
    headerWidth = on ? simpleHeaderWidth : flHeaderWidth;
    hover = {};
    resized();
    repaint();
}

void PlaylistComponent::selectTrackChannel (int track)
{
    if (onSimpleTrackSelected)
        onSimpleTrackSelected (track);
    const auto& song = engine.getSong();
    const auto kind = simpletracks::kindOf (song, track);
    if (! kind)
        return;
    int channel = -1;
    if (*kind == simpletracks::Kind::instrument || *kind == simpletracks::Kind::sampler)
        channel = song.playlistTracks[(std::size_t) track].channel;
    else if (*kind == simpletracks::Kind::drums)
    {
        const auto insert = simpletracks::insertOf (song, track);
        for (int c = 0; c < (int) song.channels.size() && channel < 0; ++c)
            if (song.channels[(std::size_t) c].mixerTrack == insert && song.channels[(std::size_t) c].instrument != InstrumentKind::audioClip)
                channel = c;
    }
    if (channel >= 0 && channel != engine.getSelectedChannel())
        engine.setSelectedChannel (channel);
}

void PlaylistComponent::selectTrack (int track)
{
    if (track < 0)
        return;
    track = juce::jmin (track, juce::jmax (0, numTracks() - 1));
    selectedTracks = { track };
    currentTrack = track;
    if (simpleHeaders)
        selectTrackChannel (track);
    // into view (vertically)
    const auto grid = gridArea();
    const auto top = trackToY (track), bottom = top + heightOf (track);
    if (top < grid.getY() || bottom > grid.getBottom())
    {
        scrollY = juce::jmax (0, scrollY + (top < grid.getY() ? top - grid.getY() : bottom - grid.getBottom()));
        updateScrollBars();
    }
    repaint();
}

int PlaylistComponent::dropLaneTrack() const
{
    return simpleHeaders ? simpletracks::nextEmptyTrack (engine.getSong()) : -1;
}

juce::Rectangle<int> PlaylistComponent::trackArrow (int track) const
{
    if (! noteedit::isGroupHeader (engine.getSong(), track))
        return {};

    const auto solo = trackSolo (track);
    const auto led = solo.isEmpty() ? trackLed (track) : solo;
    return { led.getX() - 15, led.getY() - 1, 13, led.getHeight() + 2 };
}

juce::Rectangle<int> PlaylistComponent::trackArm (int track) const
{
    // FL's View › "Show controls on audio tracks" (round 190w) hides them all
    if (! showAudioTrackControls || trackmodes::armableInsert (engine.getSong(), track) < 0 || noteedit::isFoldedAway (engine.getSong(), track))
        return {};

    if (simpleHeaders && simpletracks::kindOf (engine.getSong(), track) == simpletracks::Kind::drums)
        return {}; // (a drum track is FL's audio track only to keep its insert: nothing to record on it)
    const auto arrow = trackArrow (track);
    const auto solo = trackSolo (track);
    const auto led = solo.isEmpty() ? trackLed (track) : solo;
    if (simpleHeaders)
        return { (arrow.isEmpty() ? led.getX() : arrow.getX()) - 17, led.getY() + (led.getHeight() - 12) / 2, 12, 12 };
    return { (arrow.isEmpty() ? led.getX() : arrow.getX()) - 15, led.getY(), 12, 12 };
}

juce::Rectangle<int> PlaylistComponent::trackAudition (int track) const
{
    const auto& song = engine.getSong();
    if (noteedit::isFoldedAway (song, track) || ! comp::groupOf (song, track).isLane (track) || ! comp::isComp (song, comp::groupOf (song, track).comp))
        return {};

    // left of whatever else the row shows
    auto left = trackLed (track);
    for (const auto& other : { trackSolo (track), trackArrow (track), trackArm (track), trackInput (track) })
        if (! other.isEmpty() && other.getX() < left.getX())
            left = other;
    return { left.getX() - 15, trackLed (track).getY(), 12, 12 };
}

juce::Rectangle<int> PlaylistComponent::trackInput (int track) const
{
    const auto arm = trackArm (track);
    if (arm.isEmpty() || heightOf (track) < inputButtonMinHeight)
        return {};
    return arm.translated (-15, 0);
}

bool PlaylistComponent::isTrackSelected (int track) const
{
    return track == currentTrack || std::find (selectedTracks.begin(), selectedTracks.end(), track) != selectedTracks.end();
}

std::vector<int> PlaylistComponent::tracksForMenu (int track) const
{
    // the menu's track and, when it is one of them, the other selected tracks
    if (! isTrackSelected (track) || selectedTracks.size() < 2)
        return { track };
    auto out = selectedTracks;
    if (std::find (out.begin(), out.end(), track) == out.end())
        out.push_back (track);
    std::sort (out.begin(), out.end());
    return out;
}

void PlaylistComponent::editTrack (int track, std::function<void (PlaylistTrack&)> change)
{
    if (track < 0)
        return;

    engine.editSong ([track, change] (Song& s) {
        if ((int) s.playlistTracks.size() <= track)
            s.playlistTracks.resize ((std::size_t) track + 1);
        change (s.playlistTracks[(std::size_t) track]);
    });
}

// The track menu in FL's order (REA round 104): name and colour, size, grouping, its clips, insert / clone /
// delete, move; track modes and Consolidate since round 146a. FL items LYNX has no counterpart for (icons, performance
// settings, ghost notes) are left out.
void PlaylistComponent::showTrackMenu (int track)
{
    buildTrackMenu (track).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

juce::PopupMenu PlaylistComponent::buildTrackMenu (int track)
{
    const auto& song = engine.getSong();
    const auto count = numTracks();
    const auto info = track < (int) song.playlistTracks.size() ? song.playlistTracks[(std::size_t) track] : PlaylistTrack {};
    juce::PopupMenu menu;
    ui::addItemHeader (menu, tr ("Playlist track"), ! info.name.empty() ? juce::String::fromUTF8 (info.name.c_str()) : tr ("Track {0}", { juce::String (track + 1) }),
                       info.colour != 0 ? juce::Colour (info.colour) : juce::Colour());
    menu.addItem (theme::mnemonic (tr ("Rename and color..."), 'R'), [this, track] { renameTrack (track); });
    menu.addSubMenu (tr ("Change color"), ui::colourMenu (info.colour, [this, track] (std::uint32_t colour) {
                         editTrack (track, [colour] (PlaylistTrack& t) { t.colour = colour; });
                     }, juce::Rectangle<int> (juce::Desktop::getMousePosition().x, juce::Desktop::getMousePosition().y, 1, 1)));
    menu.addItem (tr ("Random color"), [this, track] {
        const auto& palette = lynxPalette();
        const auto c = palette[(std::size_t) juce::Random::getSystemRandom().nextInt ((int) palette.size())];
        editTrack (track, [c] (PlaylistTrack& t) { t.colour = c; });
    });
    // FL's "Change icon" (EEPLTrackIconMenuClick, REA round 168h); LYNX's own icon set
    menu.addItem (tr ("Change icon..."), [this, track, current = info.icon] {
        ui::showTrackIconPicker (juce::Rectangle<int> (juce::Desktop::getMousePosition().x, juce::Desktop::getMousePosition().y, 1, 1), current, [this, track] (int icon) {
            editTrack (track, [icon] (PlaylistTrack& t) { t.icon = icon; });
        });
    });
    // FL's "Auto name" (EEPLTrackAutoNameMenuClick → FUN_012ef490, round 168f): the name and colour of what the track plays most
    menu.addItem (theme::mnemonic (tr ("Auto name"), 'A'), [this, tracks = tracksForMenu (track)] {
        engine.editSong ([tracks] (Song& s) {
            for (const auto t : tracks)
                pltracks::autoName (s, t);
        });
    });
    menu.addItem (tr ("Reset"), [this, track] {
        editTrack (track, [] (PlaylistTrack& t) {
            t.name.clear();
            t.colour = 0;
            t.height = 0;
            t.muted = false;
        });
    });
    menu.addSeparator();
    {
        // FL's "Track mode" (round 146a): unassigned, an audio track (linked to a mixer track) or an instrument
        // track (a channel and its mixer track)
        juce::PopupMenu modes, audio, instruments;
        modes.addItem (theme::mnemonic (tr ("Unassigned"), 'U'), true, info.mode == PlaylistTrackMode::unassigned,
                       [this, track] { engine.editSong ([track] (Song& s) { trackmodes::unassign (s, track); }); });
        // FL's Audio track ▸ (REA round 190cp, FUN_0122eff0 / FUN_012ec590; round 165 issue11c f5): "New mixer
        // track(s)", then every insert as "N. name", the linked one ticked, one linked to another track greyed. A pick
        // works on the selected tracks, one undo step; an insert left behind is disarmed (FL).
        const auto assign = [this, tracks = tracksForMenu (track)] (int mixer) {
            std::vector<int> before;
            for (const auto t : tracks)
                before.push_back (trackmodes::armableInsert (engine.getSong(), t));
            int linked = 0;
            engine.editSong ([&tracks, mixer, &linked] (Song& s) { linked = trackmodes::assignAudioTracks (s, tracks, mixer); });
            for (std::size_t i = 0; i < tracks.size(); ++i)
                if (before[i] > 0 && before[i] != trackmodes::armableInsert (engine.getSong(), tracks[i]) && engine.isInsertArmed (before[i]))
                    engine.setInsertArmed (before[i], false);
            if (linked == 0 && mixer == trackmodes::newMixerTrack)
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Can't create new audio track"),
                                                        tr ("There are no available mixer tracks."));
        };
        const auto audioMenu = trackmodes::audioTrackMenu (song, track);
        audio.addItem (tr ("New mixer track(s)"), audioMenu.canAddMixerTrack, false, [assign] { assign (trackmodes::newMixerTrack); });
        audio.addSeparator();
        for (const auto& item : audioMenu.inserts)
            audio.addItem (juce::String (item.mixer) + ". " + (item.name.empty() ? tr ("Insert {0}", { juce::String (item.mixer) }) : juce::String::fromUTF8 (item.name.c_str())),
                           item.enabled, item.ticked, [assign, m = item.mixer] { assign (m); });
        modes.addSubMenu (theme::mnemonic (tr ("Audio track"), 'A'), audio, true, nullptr, info.mode == PlaylistTrackMode::audio);
        // FL's Instrument track › (EEPLTrackModeInstrMenu, round 190dx): "Use existing channel" ›, a line, "More
        // plugins..." (FL's default item), a line, then the generator list of the Channel rack's Insert menu
        juce::PopupMenu existing;
        for (int c = 0; c < (int) song.channels.size(); ++c)
        {
            const auto kind = song.channels[(std::size_t) c].instrument;
            if (kind == InstrumentKind::audioClip || kind == InstrumentKind::controller)
                continue;
            existing.addItem (juce::String::fromUTF8 (song.channels[(std::size_t) c].name.c_str()), true,
                                 info.mode == PlaylistTrackMode::instrument && info.channel == c, [this, track, c] {
                                     bool ok = false;
                                     engine.editSong ([track, c, &ok] (Song& s) { ok = trackmodes::makeInstrumentTrack (s, track, c); });
                                     if (! ok)
                                         juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Can't create new instrument track"),
                                                                                 tr ("There are no available mixer tracks."));
                                 });
        }
        instruments.addSubMenu (theme::mnemonic (tr ("Use existing channel"), 'U'), existing, existing.getNumItems() > 0);
        instruments.addSeparator();
        instruments.addItem (tr ("More plugins..."), [this, track] { morePluginsForTrack (track); });
        instruments.addSeparator();
        quickpick::addGeneratorMenu (instruments, engine, [this, track] (ChannelDef c) { instrumentForTrack (track, std::move (c)); }, nullptr, nullptr, this);
        quickpick::addFolderViewItems (instruments, engine);
        modes.addSubMenu (theme::mnemonic (tr ("Instrument track"), 'I'), instruments, true, nullptr, info.mode == PlaylistTrackMode::instrument);
        menu.addSubMenu (theme::mnemonic (tr ("Track mode"), 'T'), modes);
    }
    {
        // FL's "Performance settings" (round 165m [DFM]): how the track's clips in the performance zone launch and play
        using P = TrackPerformance;
        const auto p = info.performance;
        juce::PopupMenu perfMenu;
        // FL's handler (0x1212fe0 → FUN_012ead80) sets the item on every selected track, in one undo step
        auto set = [this, tracks = tracksForMenu (track)] (std::function<void (TrackPerformance&)> change) {
            engine.editSong ([tracks, change] (Song& s) {
                for (const auto t : tracks)
                {
                    if ((int) s.playlistTracks.size() <= t)
                        s.playlistTracks.resize ((std::size_t) t + 1);
                    change (s.playlistTracks[(std::size_t) t].performance);
                }
            });
        };
        perfMenu.addSectionHeader (tr ("Press"));
        for (auto [value, text] : { std::pair { P::Press::retrigger, TR_NOOP ("Retrigger") }, { P::Press::holdStop, TR_NOOP ("Hold & stop") },
                                    { P::Press::holdMotion, TR_NOOP ("Hold & motion") }, { P::Press::latch, TR_NOOP ("Latch") } })
            perfMenu.addItem (tr (text), true, p.press == value, [set, value = value] { set ([value] (P& x) { x.press = value; }); });
        perfMenu.addSectionHeader (tr ("Motion"));
        for (auto [value, text] : { std::pair { P::Motion::stay, TR_NOOP ("Stay") }, { P::Motion::oneShot, TR_NOOP ("One shot") },
                                    { P::Motion::marchWrap, TR_NOOP ("March & wrap") }, { P::Motion::marchStay, TR_NOOP ("March & stay") },
                                    { P::Motion::marchStop, TR_NOOP ("March & stop") }, { P::Motion::random, TR_NOOP ("Random") },
                                    { P::Motion::exclusiveRandom, TR_NOOP ("Exclusive random") } })
            perfMenu.addItem (tr (text), true, p.motion == value, [set, value = value] { set ([value] (P& x) { x.motion = value; }); });
        const std::pair<P::Sync, const char*> syncs[] = { { P::Sync::off, TR_NOOP ("Off") }, { P::Sync::quarterBeat, TR_NOOP ("1/4 beat") },
                                                          { P::Sync::halfBeat, TR_NOOP ("1/2 beat") }, { P::Sync::beat, TR_NOOP ("1 beat") },
                                                          { P::Sync::twoBeats, TR_NOOP ("2 beats") }, { P::Sync::fourBeats, TR_NOOP ("4 beats") },
                                                          { P::Sync::autoLength, TR_NOOP ("Auto") } };
        perfMenu.addSectionHeader (tr ("Trigger sync"));
        for (const auto& [value, text] : syncs)
            perfMenu.addItem (tr (text), true, p.triggerSync == value, [set, value = value] { set ([value] (P& x) { x.triggerSync = value; }); });
        perfMenu.addSeparator();
        perfMenu.addItem (tr ("Queue"), true, p.queue, [set] { set ([] (P& x) { x.queue = ! x.queue; }); });
        perfMenu.addItem (tr ("Tolerant"), true, p.tolerant, [set] { set ([] (P& x) { x.tolerant = ! x.tolerant; }); });
        perfMenu.addSectionHeader (tr ("Position sync"));
        for (const auto& [value, text] : syncs)
            perfMenu.addItem (tr (text), true, p.positionSync == value, [set, value = value] { set ([value] (P& x) { x.positionSync = value; }); });
        menu.addSubMenu (theme::mnemonic (tr ("Performance settings"), 'P'), perfMenu);
    }
    menu.addSeparator();
    {
        // FL's sizes are relative to the default row; dragging the divider under a header sets any size (P18)
        juce::PopupMenu size, all;
        const auto current = heightOf (track);

        for (int percent : { 33, 50, 100, 150, 200 })
        {
            const auto h = polish::clampTrackHeight ((int) std::lround (trackHeight * percent / 100.0));
            size.addItem (juce::String (percent) + "%", true, current == scaledHeight (h), [this, track, h] { setTrackHeights ({ track }, h); });
            all.addItem (juce::String (percent) + "%", [this, count, h] {
                std::vector<int> tracks;
                for (int t = 0; t < count; ++t)
                    tracks.push_back (t);
                setTrackHeights (tracks, h);
            });
        }
        size.addSeparator();
        size.addSubMenu (tr ("Every track"), all);
        menu.addSubMenu (theme::mnemonic (tr ("Size"), 'S'), size);
    }
    // FL's "Lock to this size" (EEPLTrackLockMenuClick 0x1228cb0, round 168f): the row keeps the height it has now
    menu.addItem (theme::mnemonic (tr ("Lock to this size"), 'L'), true, info.lockedHeight > 0, [this, track, tracks = tracksForMenu (track)] {
        const auto shown = heightOf (track);
        engine.editSong ([tracks, shown] (Song& s) { pltracks::toggleSizeLock (s, tracks, shown); });
        refreshTrackLayout();
        updateScrollBars();
        repaint();
    });
    menu.addSeparator();
    {
        const auto header = noteedit::groupHeaderOf (song, track);
        const auto member = info.grouped && track > 0;
        const auto folded = header >= 0 && header < (int) song.playlistTracks.size() && song.playlistTracks[(std::size_t) header].collapsed;
        menu.addItem (theme::mnemonic (tr ("Group with above track"), 'G'), track > 0 && ! member, member, [this, track] {
            // (round 190cg) in the Clip-launch workflow the group is a bus: its members play through the header's mixer track
            if (liveStyle)
                engine.editSongStructural ([track] (Song& s) {
                    noteedit::groupWithAbove (s, track);
                    arrange::makeGroupBus (s, track);
                });
            else
                engine.editSong ([track] (Song& s) { noteedit::groupWithAbove (s, track); });
        });
        // the same for a group made in another layout; ticked while the group plays through its header's mixer track
        if (const auto group = arrange::groupOf (song, track); group.valid() && (liveStyle || arrange::groupBus (song, track) >= 0))
            menu.addItem (tr ("Group plays through its own mixer track"), arrange::groupBus (song, track) < 0, arrange::groupBus (song, track) >= 0,
                          [this, track] { engine.editSongStructural ([track] (Song& s) { arrange::makeGroupBus (s, track); }); });
        // round 190cg: Live's track fold [live-exe-strings.txt:1645415…1645430 sMenuFoldSelectedTrack(s) / sMenuUnfoldAllTracks]
        {
            const auto anyFolded = std::any_of (song.playlistTracks.begin(), song.playlistTracks.end(), [] (const PlaylistTrack& t) { return t.folded; });
            if (liveStyle || anyFolded)
            {
                const auto isFolded = arrange::isFolded (song, track);
                menu.addItem (tr ("Fold track"), true, isFolded, [this, track, isFolded] {
                    auto tracks = selectedTracks;
                    if (std::find (tracks.begin(), tracks.end(), track) == tracks.end())
                        tracks = { track };
                    engine.editSong ([tracks, isFolded] (Song& s) { arrange::setFolded (s, std::vector<int> (tracks.begin(), tracks.end()), ! isFolded); });
                    resized();
                });
                menu.addItem (tr ("Unfold all tracks"), anyFolded, false, [this] {
                    engine.editSong ([] (Song& s) { arrange::setAllFolded (s, false); });
                    resized();
                });
            }
        }
        menu.addItem (theme::mnemonic (tr ("Ungroup"), 'n'), member, false, [this, track] { engine.editSong ([track] (Song& s) { noteedit::ungroupTrack (s, track); }); });
        menu.addItem (tr ("Auto color group"), header >= 0, false, [this, track] { engine.editSong ([track] (Song& s) { noteedit::colourGroup (s, track); }); });
        menu.addItem (tr ("Collapse grouped tracks"), header >= 0, folded, [this, track, folded] {
            engine.editSong ([track, folded] (Song& s) { noteedit::setGroupCollapsed (s, track, ! folded); });
        });
        // round 177c: comping starts here, recording never moves tracks
        if (comp::selectionSwipes (comp::swipeStyle) && comp::groupOf (song, track).isLane (track) && comp::isComp (song, comp::groupOf (song, track).comp))
            menu.addItem (tr ("Send the time selection to the comp"), exportopts::hasSelection (song), false,
                          [this, track] { swipeTimeSelectionToComp (track); });
        menu.addItem (tr ("Comp these takes"), comp::canComp (song, track), false, [this, track] {
            setSelection ({});
            engine.editSong ([track] (Song& s) { comp::compTakes (s, track); });
            sortClips();
        });
        const auto compGroup = comp::groupOf (song, track);
        if (comp::isComp (song, compGroup.comp))
            menu.addItem (tr ("Flatten comp (keeps what plays, removes the take lanes)"), [this, head = compGroup.comp] {
                setSelection ({});
                engine.editSong ([head] (Song& s) { comp::flatten (s, head); });
                sortClips();
            });
    }
    menu.addSeparator();
    {
        // the sources of the track's clips: choosing one makes it what the pencil paints (FL's "Current clip source")
        juce::PopupMenu sources;
        std::vector<std::pair<ClipKind, int>> seen;
        for (const auto& c : song.playlist)
        {
            if (c.track != track)
                continue;
            const auto key = std::pair { c.kind, c.kind == ClipKind::pattern ? c.pattern : c.kind == ClipKind::audio ? c.channel : c.automation };
            if (std::find (seen.begin(), seen.end(), key) != seen.end())
                continue;
            seen.push_back (key);
            juce::String name;
            if (key.first == ClipKind::pattern && key.second >= 0 && key.second < (int) song.patterns.size())
                name = juce::String::fromUTF8 (song.patterns[(std::size_t) key.second].name.c_str());
            else if (key.first == ClipKind::automation && key.second >= 0 && key.second < (int) song.automations.size())
                name = juce::String::fromUTF8 (song.automations[(std::size_t) key.second].name.c_str());
            else if (key.second >= 0 && key.second < (int) song.channels.size())
                name = juce::String::fromUTF8 (song.channels[(std::size_t) key.second].name.c_str());
            const auto ticked = brush.kind == key.first && (key.first == ClipKind::pattern ? song.currentPattern == key.second : brush.index == key.second);
            sources.addItem (name, true, ticked, [this, key] {
                brush.kind = key.first;
                brush.index = key.first == ClipKind::pattern ? -1 : key.second;
                if (key.first == ClipKind::pattern)
                    engine.editSong ([p = key.second] (Song& s) { s.currentPattern = p; }, false);
                engine.sendChangeMessage();
            });
        }
        menu.addSubMenu (tr ("Current clip source"), sources, ! seen.empty());
        // FL's "Lock to content" (EEPLTrackClipLockMenuClick → FUN_012e7a90, round 168f): painting on the track places
        // its own content
        menu.addItem (tr ("Lock to content"), true, info.lockedToContent, [this, tracks = tracksForMenu (track)] {
            engine.editSong ([tracks] (Song& s) { pltracks::toggleLockToContent (s, tracks); });
        });
        auto muteAll = [this, track] (bool mute) {
            engine.editSong ([track, mute] (Song& s) {
                for (auto& c : s.playlist)
                    if (c.track == track)
                        c.muted = mute;
            });
        };
        const auto hasClips = ! seen.empty();
        {
            // FL's "Consolidate track(s)" (round 146a): the track's clips rendered into one audio clip
            juce::PopupMenu consolidate;
            consolidate.addItem (theme::mnemonic (tr ("From track start"), 'F'), hasClips, false, [this, track] { consolidatePlaylistTrack (engine, track, false); });
            consolidate.addItem (theme::mnemonic (tr ("From song start"), 's'), hasClips, false, [this, track] { consolidatePlaylistTrack (engine, track, true); });
            menu.addSubMenu (theme::mnemonic (tr ("Consolidate track"), 'o'), consolidate, hasClips);
        }
        menu.addItem (tr ("Merge clips"), hasClips, false, [this, track] {
            setSelection ({});
            engine.editSong ([track] (Song& s) { clipops::mergeTrackClips (s, track); });
            sortClips();
        });
        // FL's "Send pattern instances here" (EEPLTrackSendPatMenuClick → FUN_012db4e0, round 168f): the selected pattern
        // clips, or every clip of the current pattern, move to this track
        menu.addItem (tr ("Send pattern instances here"), [this, track] {
            const auto chosen = selection;
            engine.editSong ([track, chosen] (Song& s) { pltracks::sendPatternInstancesHere (s, track, chosen); });
            sortClips();
        });
        menu.addItem (tr ("Mute all clips"), hasClips, false, [muteAll] { muteAll (true); });
        menu.addItem (tr ("Unmute all clips"), hasClips, false, [muteAll] { muteAll (false); });
    }
    // round 190ch (LYNX's own): one click cleans a recorded voice, a chain of LYNX's effects on the track's insert
    if (const auto insert = simpleHeaders ? simpletracks::insertOf (engine.getSong(), track) : -1; insert > 0)
        menu.addSubMenu (tr ("Clean voice"), ui::cleanVoiceMenu (engine, insert));
    menu.addSeparator();
    menu.addItem (theme::mnemonic (tr ("Insert one"), 'I'), [this, track] {
        setSelection ({});
        engine.editSong ([track] (Song& s) { noteedit::insertTrack (s, track); });
    });
    menu.addItem (theme::mnemonic (tr ("Clone..."), 'o'), [this, track] { cloneTracksWithDialog (tracksForMenu (track)); });
    menu.addItem (theme::mnemonic (tr ("Delete"), 'e'), [this, track] {
        setSelection ({});
        engine.editSong ([track] (Song& s) { noteedit::removeTrack (s, track); });
    });
    menu.addSeparator();
    menu.addItem (theme::mnemonic (tr ("Move up"), 'u'), track > 0, false, [this, track] { engine.editSong ([track] (Song& s) { noteedit::swapTracks (s, track, track - 1); }); });
    menu.addItem (theme::mnemonic (tr ("Move down"), 'd'), track + 1 < count, false, [this, track] { engine.editSong ([track] (Song& s) { noteedit::swapTracks (s, track, track + 1); }); });
    return menu;
}

void PlaylistComponent::renameTrack (int track)
{
    const auto& tracks = engine.getSong().playlistTracks;
    const auto current = track < (int) tracks.size() ? juce::String::fromUTF8 (tracks[(std::size_t) track].name.c_str()) : juce::String();
    const auto colour = track < (int) tracks.size() ? tracks[(std::size_t) track].colour : 0u;
    showNameAndColourDialog (tr ("Rename track"), current.isNotEmpty() ? current : tr ("Track {0}", { juce::String (track + 1) }), colour,
                             [this, track] (const juce::String& newName, std::uint32_t newColour) {
                                 const auto name = newName.trim().toStdString();
                                 const auto together = simpleHeaders && simpletracks::kindOf (engine.getSong(), track).has_value();
                                 engine.editSong ([track, name, newColour, together] (Song& s) {
                                     if ((int) s.playlistTracks.size() <= track)
                                         s.playlistTracks.resize ((std::size_t) track + 1);
                                     if (together) // (the Simple workflow: the track, its insert and its channel, round 184a)
                                     {
                                         simpletracks::rename (s, track, name);
                                         simpletracks::recolour (s, track, newColour);
                                         return;
                                     }
                                     s.playlistTracks[(std::size_t) track].name = name;
                                     s.playlistTracks[(std::size_t) track].colour = newColour;
                                 });
                             });
}

juce::String PlaylistComponent::markerLabel (const Marker& m)
{
    auto label = juce::String::fromUTF8 (m.name.c_str());
    if (const auto fraction = juce::String (m.numerator) + "/" + juce::String (m.denominator); m.isSignature() && label != fraction)
        label = fraction + (label.isEmpty() ? "" : " " + label);
    // FL's marker actions: a symbol before the name (unnamed: the action's name)
    if (! m.isSignature() && m.action != 0 && m.action != Marker::punchIn && m.action != Marker::punchOut)
    {
        if (label.isEmpty())
            label = tr (markers::actionName (m.action));
        label = juce::String::fromUTF8 (markers::actionTag (m.action)) + " " + label;
    }
    if (label.isEmpty())
        label = "*";
    return label;
}

int PlaylistComponent::markerTagAt (int x, int y) const
{
    // the tags hang at the bottom of the ruler (see paint)
    if (y < rulerTop + timelineHeight - 12 || y >= headerHeight)
        return -1;
    const auto& list = engine.getSong().markers();
    const auto f = theme::font (theme::type::caption, true);
    for (int i = (int) list.size(); --i >= 0;) // the later tag is drawn on top
    {
        const auto mx = tickToX ((double) list[(std::size_t) i].tick);
        const auto width = (double) juce::GlyphArrangement::getStringWidthInt (f, markerLabel (list[(std::size_t) i])) + 8.0;
        if (x >= mx - 2.0 && x <= mx + width)
            return i;
    }
    return -1;
}

void PlaylistComponent::showRulerMenu (int x)
{
    const auto& song = engine.getSong();
    const auto tick = juce::jmax<std::int64_t> (0, snap (xToTick (x), true));

    // marker near the click (up to 6 px)
    int near = -1;
    for (int i = 0; i < (int) song.markers().size(); ++i)
        if (std::abs (tickToX ((double) song.markers()[(std::size_t) i].tick) - x) <= 6.0)
            near = i;

    juce::PopupMenu menu;
    menu.addItem (tr ("Add marker here..."), [this, tick] { editMarkerName (tick, -1); });

    juce::PopupMenu signatures;
    for (auto [n, d] : { std::pair { 2, 4 }, { 3, 4 }, { 4, 4 }, { 5, 4 }, { 6, 4 }, { 7, 4 }, { 6, 8 }, { 7, 8 }, { 9, 8 }, { 12, 8 } })
        signatures.addItem (juce::String (n) + "/" + juce::String (d), [this, tick, n = n, d = d] {
            engine.editSong ([tick, n, d] (Song& s) { s.addMarker ({ tick, {}, n, d }); });
        });
    menu.addSubMenu (tr ("Time signature change here"), signatures);
    {
        // round 190cg [live-exe-strings.txt:1667947 sMenuClearTimeSignatureChanges]: the one at the song's start stays
        const auto later = std::any_of (song.markers().begin(), song.markers().end(), [] (const Marker& m) { return m.numerator > 0 && m.tick > 0; });
        menu.addItem (tr ("Delete all time signature changes"), later, false, [this] {
            engine.editSong ([] (Song& s) {
                auto& m = s.markers();
                m.erase (std::remove_if (m.begin(), m.end(), [] (const Marker& x) { return x.numerator > 0 && x.tick > 0; }), m.end());
            });
        });
    }
    // FL's marker actions "Start recording" / "Stop recording" (punch in / out; Record menu › Enable recording markers)
    menu.addItem (tr ("Add a Start recording marker here"), [this, tick] {
        engine.editSong ([tick] (Song& s) {
            Marker m;
            m.tick = tick;
            m.name = tr ("Start recording").toStdString();
            m.action = Marker::punchIn;
            s.addMarker (m);
        });
    });
    menu.addItem (tr ("Add a Stop recording marker here"), [this, tick] {
        engine.editSong ([tick] (Song& s) {
            Marker m;
            m.tick = tick;
            m.name = tr ("Stop recording").toStdString();
            m.action = Marker::punchOut;
            s.addMarker (m);
        });
    });
    // FL's "Start" marker: the song starts there; in Performance mode the clips before it are the performance zone
    menu.addItem (tr ("Add a Start marker here (the song starts here; Performance mode launches the clips before it)"), [this, tick] {
        engine.editSong ([tick] (Song& s) {
            Marker m;
            m.tick = tick;
            m.name = tr ("Start").toStdString();
            m.action = Marker::songStart;
            s.addMarker (m);
        });
    });
    if (song.performanceZoneEnd() > 0 && tick < song.performanceZoneEnd())
    {
        // a marker of the zone: launch every clip of its column (from this marker to the next one)
        std::int64_t from = 0, to = song.performanceZoneEnd();
        for (const auto& m : song.markers())
        {
            if (m.tick <= tick)
                from = juce::jmax (from, m.tick);
            else
                to = juce::jmin (to, m.tick);
        }
        menu.addItem (tr ("Launch this column's clips"), engine.getTransport().playing.load(), false, [this, from, to] {
            perf::Command c;
            c.type = perf::Command::Type::launchColumn;
            c.columnStart = from;
            c.columnEnd = to;
            engine.performanceCommand (c);
        });
        menu.addItem (tr ("Stop all performance clips"), engine.getTransport().playing.load(), false, [this] {
            perf::Command c;
            c.type = perf::Command::Type::stopAll;
            engine.performanceCommand (c);
        });
    }

    // FL's Playlist menu › Time markers (REA_rodada165f-markers §1.4)
    {
        juce::PopupMenu timeMarkers;
        juce::PopupMenu every;
        for (auto [bars, text] : { std::pair { 1, TR_NOOP ("Bar") }, { 2, TR_NOOP ("2 bars") }, { 4, TR_NOOP ("4 bars") }, { 8, TR_NOOP ("8 bars") } })
            every.addItem (tr (text), [this, bars = bars] {
                engine.editSong ([bars] (Song& s) { markers::addEvery (s, bars); });
            });
        timeMarkers.addItem (tr ("Add one...") + "\tAlt+T", [this] { runAction ("playlist.markerAdd"); });
        timeMarkers.addItem (tr ("Add auto") + "\tCtrl+T", [this] { runAction ("playlist.markerAuto"); });
        timeMarkers.addSubMenu (tr ("Add one every"), every);
        timeMarkers.addItem (tr ("Add jump to next bar"), [this, tick] {
            engine.editSong ([tick] (Song& s) { markers::addJumpToNextBar (s, tick); });
        });
        timeMarkers.addItem (tr ("Add time signature change...") + "\tShift+Alt+T", [this] { runAction ("playlist.markerSignature"); });
        timeMarkers.addItem (tr ("Place loop") + "\tShift+T", [this] { runAction ("playlist.markerLoop"); });
        timeMarkers.addItem (tr ("Change to marker loop"), song.hasLoop(), false, [this] {
            engine.editSong ([] (Song& s) { markers::changeToMarkerLoop (s); });
        });
        menu.addSubMenu (tr ("Time markers"), timeMarkers);

        juce::PopupMenu quantize;
        const auto current = engine.quantizeSongJumps.load();
        for (auto [value, text] : { std::pair { 0, TR_NOOP ("Off") }, { 1, TR_NOOP ("Beats") }, { 2, TR_NOOP ("Bars") }, { 3, TR_NOOP ("2 bars") },
                                    { 4, TR_NOOP ("4 bars") }, { 5, TR_NOOP ("8 bars") } })
            quantize.addItem (tr (text), true, current == value, [this, value = value] { engine.quantizeSongJumps.store (value); });
        menu.addSubMenu (tr ("Quantize song jumps"), quantize);
    }

    if (near >= 0)
    {
        menu.addSeparator();
        menu.addSectionHeader (markerLabel (song.markers()[(std::size_t) near]));
        menu.addItem (tr ("Rename marker..."), [this, near] { editMarkerName (0, near); });
        menu.addItem (tr ("Place loop (from this marker to the next)"), [this, near] {
            engine.editSong ([near] (Song& s) {
                if (near >= (int) s.markers().size())
                    return;
                const auto from = s.markers()[(std::size_t) near].tick;
                std::int64_t to = s.songLength();
                for (const auto& m : s.markers())
                    if (m.tick > from)
                    {
                        to = m.tick;
                        break;
                    }
                if (to > from)
                {
                    s.loopStart = from;
                    s.loopEnd = to;
                }
            }, false);
        });
        {
            // FL's marker "Action" list, in FL's order (the values are FL's .flp marker types)
            juce::PopupMenu action;
            const auto& marker = song.markers()[(std::size_t) near];
            const auto current = marker.isSignature() ? Marker::timeSignature : marker.action;
            // (FL's Playlist shows these 9; Pattern length, Bar override and Key/Scale are its Piano Roll markers' actions)
            for (auto [value, text] : { std::pair { Marker::none, TR_NOOP ("None (plain marker)") }, { Marker::start, TR_NOOP ("Start") },
                                        { Marker::songLoop, TR_NOOP ("Loop") }, { Marker::markerLoop, TR_NOOP ("Marker loop") },
                                        { Marker::markerSkip, TR_NOOP ("Marker skip") }, { Marker::markerPause, TR_NOOP ("Marker pause") },
                                        { Marker::timeSignature, TR_NOOP ("Time signature") }, { Marker::punchIn, TR_NOOP ("Start recording") },
                                        { Marker::punchOut, TR_NOOP ("Stop recording") } })
                action.addItem (tr (text), true, current == value, [this, near, value = value] {
                    engine.editSong ([near, value] (Song& s) {
                        if (near < (int) s.markers().size())
                            markers::setAction (s, s.markers()[(std::size_t) near], value);
                    });
                });
            menu.addSubMenu (tr ("Marker action"), action);
        }
        menu.addItem (tr ("Remove marker"), [this, near] {
            engine.editSong ([near] (Song& s) {
                if (near < (int) s.markers().size())
                    s.markers().erase (s.markers().begin() + near);
            });
        });
    }

    if (song.hasLoop())
    {
        menu.addSeparator();
        menu.addItem (tr ("Remove loop"), [this] { engine.editSong ([] (Song& s) { s.loopStart = s.loopEnd = 0; }, false); });
        // round 190cg: the loop switched off keeps its range
        menu.addItem (tr ("Loop the time selection"), true, song.loopPlays(), [this] { runAction ("playlist.loopSwitch"); });
    }

    menu.addSeparator();
    {
        // FL's Select submenu (REA round 153; FL's full list and order, round 189e)
        menu.addSubMenu (tr ("Select"), selectMenu());
    }
    menu.addItem (tr ("Ripple editing (resizing and deleting move the later clips)") + "\tShift+S", true, ripple, [this] { setRipple (! ripple); });
    {
        auto anyAudio = false;
        for (auto i : selection)
            anyAudio = anyAudio || (i >= 0 && i < (int) song.playlist.size() && song.playlist[(std::size_t) i].kind == ClipKind::audio);
        menu.addItem (tr ("Pitch selected audio clips one semitone down") + "\t7", anyAudio, false, [this] { runAction ("playlist.pitchClipsDown"); });
        menu.addItem (tr ("Pitch selected audio clips one semitone up") + "\t8", anyAudio, false, [this] { runAction ("playlist.pitchClipsUp"); });
        menu.addItem (tr ("Reverse selected audio clips") + "\t9", anyAudio, false, [this] { runAction ("playlist.reverseClips"); });
    }
    menu.addItem (tr ("Automatic crossfades (current project)"), true, engine.getSong().autoCrossfades, [this] { runAction ("playlist.autoCrossfades"); });
    // FL's legacy Pattern Blocks (round 170f)
    menu.addItem (tr ("Pattern blocks (current project)"), true, engine.getSong().patternBlocksOn, [this] { runAction ("playlist.patternBlocks"); });
    menu.addItem (tr ("Turn pattern blocks into clips"), ! engine.getSong().blocks.empty(), false, [this] { runAction ("playlist.blocksToClips"); });
    menu.addItem (tr ("Automation lanes"), true, autolanes::shown(), [this] { runAction ("playlist.automationLanes"); }); // round 177b
    {
        juce::PopupMenu zoom;
        zoom.addItem (tr ("Zoom in") + "\tPageUp", [this] { runAction ("playlist.zoomIn"); });
        zoom.addItem (tr ("Zoom out") + "\tPageDown", [this] { runAction ("playlist.zoomOut"); });
        zoom.addItem (tr ("Zoom: one bar") + "\tShift+1", [this] { runAction ("playlist.zoom1"); });
        zoom.addItem (tr ("Zoom: four bars") + "\tShift+2", [this] { runAction ("playlist.zoom2"); });
        zoom.addItem (tr ("Zoom: sixteen bars") + "\tShift+3", [this] { runAction ("playlist.zoom3"); });
        zoom.addItem (tr ("Zoom to show everything") + "\tShift+4", [this] { runAction ("playlist.zoomAll"); });
        zoom.addItem (tr ("Zoom to the selection") + "\tShift+5", [this] { runAction ("playlist.zoomSelection"); });
        zoom.addItem (tr ("Centre the view on the playhead") + "\tShift+0", [this] { runAction ("playlist.centrePlayhead"); });
        menu.addSubMenu (tr ("Zoom"), zoom);
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

void PlaylistComponent::editMarkerName (std::int64_t tick, int existing)
{
    const auto& markers = engine.getSong().markers();
    const auto current = existing >= 0 && existing < (int) markers.size() ? juce::String::fromUTF8 (markers[(std::size_t) existing].name.c_str())
                                                                          : tr ("Marker");
    auto* window = new juce::AlertWindow (existing >= 0 ? tr ("Rename marker") : tr ("New marker"), {}, juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("name", current);
    window->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    window->enterModalState (true, juce::ModalCallbackFunction::create ([this, tick, existing, window] (int result) {
                                 if (result != 1)
                                     return;

                                 const auto name = window->getTextEditorContents ("name").trim().toStdString();
                                 engine.editSong ([tick, existing, name] (Song& s) {
                                     if (existing < 0)
                                         s.addMarker ({ tick, name, 0, 4 });
                                     else if (existing < (int) s.markers().size())
                                         s.markers()[(std::size_t) existing].name = name;
                                 });
                             }),
                             true);
}

void PlaylistComponent::showClipMenu (int index)
{
    auto menu = buildClipMenu (index, getMouseXYRelative().x);
    if (menu.getNumItems() > 0)
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

juce::PopupMenu PlaylistComponent::getClipMenuForSnapshot (int index)
{
    if (index < 0 || index >= (int) engine.getSong().playlist.size())
        return {};
    const auto& c = engine.getSong().playlist[(std::size_t) index];
    return buildClipMenu (index, (int) tickToX ((double) c.start + (double) c.length / 2.0));
}

// "Follow the project key" (with what it does now) and "Loop key ▸" for an audio clip (round 190ds, core/KeyFollow.h)
void PlaylistComponent::addKeyFollowItems (juce::PopupMenu& menu, int index, const std::vector<int>& targets)
{
    const auto& song = engine.getSong();
    const auto& clip = song.playlist[(std::size_t) index];
    const auto key = keyfollow::loopKeyOf (song, clip.channel);
    const auto keyText = [] (const keyfollow::LoopKey& k) {
        return juce::String (projectkey::rootName (k.root)) + " " + (k.minor ? tr ("minor") : tr ("major"));
    };
    juce::String state;
    if (! key)
        state = tr ("the loop's key is not known");
    else if (! projectkey::get (song))
        state = tr ("no project key");
    else if (clip.followKey)
    {
        const auto shift = keyfollow::shiftOf (song, clip);
        state = shift == 0 ? tr ("already in key") : tr ("moved {0} semitones", { (shift > 0 ? "+" : "") + juce::String (shift) });
    }
    menu.addSeparator();
    menu.addItem (tr ("Follow the project key") + (state.isEmpty() ? juce::String() : "  (" + state + ")"), key.has_value(), clip.followKey,
                  [this, targets, on = ! clip.followKey] { engine.editSong ([targets, on] (Song& s) { keyfollow::setFollow (s, targets, on); }); });
    juce::PopupMenu keys;
    const auto channel = clip.channel;
    for (const bool minor : { false, true })
    {
        juce::PopupMenu roots;
        for (int r = 0; r < 12; ++r)
            roots.addItem (juce::String (projectkey::rootName (r)), true, key && key->root == r && key->minor == minor, [this, channel, r, minor] {
                engine.editSong ([channel, r, minor] (Song& s) { keyfollow::setLoopKey (s, channel, keyfollow::LoopKey { r, minor }, 3); });
            });
        keys.addSubMenu (minor ? tr ("Minor") : tr ("Major"), roots);
    }
    keys.addSeparator();
    keys.addItem (tr ("Not known"), true, ! key, [this, channel] { engine.editSong ([channel] (Song& s) { keyfollow::setLoopKey (s, channel, std::nullopt, 0); }); });
    const auto source = song.channels[(std::size_t) channel].sampler.loopKeySource;
    menu.addSubMenu (tr ("Loop key") + (key ? ": " + keyText (*key) + (source == 1 ? " (" + tr ("from its name") + ")" : source == 2 ? " (" + tr ("detected") + ")" : juce::String())
                                            : juce::String()),
                     keys);
    menu.addSeparator();
}

namespace
{
// Live's audio clip › Slice to New MIDI Track (round 190ec; dialog as Live's since round 190el, 190ei B6): "Create one
// slice per" (Warp Marker, Transient, Bar, 1/2 Note … 1/32 Note), the count line, "Slicing Preset" and "Preserve warped
// timing", then a new channel holding a Drum Rack named after the clip with one pad per slice ("Slice 1"… from C1) and
// a pattern that replays the slices where they were, on a new track under the clip, all named after the clip
// (core/Rack.h). The presets are LYNX's own (Live's slicing presets are Live content): Default (the facts of Live's
// Default: gated, choke group 1) or One-shot.
struct ClipSlices
{
    std::vector<float> slices; // starts, fractions of the file
    float from = 0.0f, to = 1.0f;
    double beats = 0.0;        // the clip region's length
    std::function<double (float)> tickOf;
    std::string name, samplePath;
    std::uint32_t colour = 0;
    bool ok = false;
};

ClipSlices clipSlices (AudioEngine& engine, int clipIndex, rack::SlicePer per, bool preserveWarpedTiming)
{
    ClipSlices out;
    const auto& song = engine.getSong();
    if (clipIndex < 0 || clipIndex >= (int) song.playlist.size())
        return out;
    const auto clip = song.playlist[(std::size_t) clipIndex];
    if (clip.kind != ClipKind::audio || clip.channel < 0 || clip.channel >= (int) song.channels.size())
        return out;
    const auto channel = song.channels[(std::size_t) clip.channel];
    const auto sample = engine.getSampleLibrary().load (juce::String::fromUTF8 (channel.samplePath.c_str()));
    if (sample == nullptr || sample->frames() == 0 || sample->sampleRate <= 0.0 || clipops::playsBackwards (channel, clip))
        return out;
    // the part of the file the clip plays, as fractions of the file (the clip's own timing: its trim, stretch and pitch)
    const auto fileFrames = (double) sample->frames();
    const auto span = clipops::clipSpan (channel, sample);
    const auto perTick = clipops::framesPerTick (channel, clip, (double) span.frames, sample->sampleRate, song.bpm);
    if (perTick <= 0.0)
        return out;
    out.from = (float) std::clamp (((double) span.first + (double) clip.offset * perTick) / fileFrames, 0.0, 1.0);
    out.to = (float) std::clamp (((double) span.first + (double) (clip.offset + clip.length) * perTick) / fileFrames, (double) out.from, 1.0);
    out.beats = (double) clip.length / (double) ppq;
    switch (per)
    {
        case rack::SlicePer::transient:
            out.slices = rack::slicesInRegion (detectSlices (*sample, 0.5f, rack::drumPadCount), out.from, out.to);
            break;
        case rack::SlicePer::warpMarker: // (seconds of the file)
        {
            std::vector<float> marks;
            for (const auto& w : channel.sampler.warp)
                marks.push_back ((float) (w.seconds * sample->sampleRate / fileFrames));
            out.slices = rack::slicesInRegion (marks, out.from, out.to);
            break;
        }
        default: // a bar or a note division (a whole note = 4 beats), in the clip's time
        {
            const auto secondsPerSlice = rack::slicePerBeats (per, song.beatsPerBar) * ppq * perTick / sample->sampleRate;
            out.slices = rack::gridSlices (fileFrames / sample->sampleRate, out.from, out.to, secondsPerSlice);
            break;
        }
    }
    // Preserve warped timing on: the notes where the clip plays each slice (its stretch and pitch); off: at the file's
    // own timing from the clip's start, at the song tempo
    const auto fromFrame = (double) span.first + (double) clip.offset * perTick;
    if (preserveWarpedTiming)
        out.tickOf = [=] (float fraction) { return ((double) fraction * fileFrames - (double) span.first) / perTick - (double) clip.offset; };
    else
        out.tickOf = [=, rate = sample->sampleRate, bpm = song.bpm] (float fraction) {
            return ((double) fraction * fileFrames - fromFrame) / rate * std::max (1.0, bpm) / 60.0 * (double) ppq;
        };
    const auto stem = juce::File (juce::String::fromUTF8 (channel.samplePath.c_str())).getFileNameWithoutExtension().toStdString();
    out.name = ! channel.name.empty() ? channel.name : stem; // (the audio clip's name: its channel's, as the Playlist shows it)
    out.samplePath = channel.samplePath;
    out.colour = channel.colour;
    out.ok = true;
    return out;
}

void sliceClipToNewTrack (AudioEngine& engine, int clipIndex, rack::SlicePer per, bool oneShot, bool preserveWarpedTiming, std::function<void()> done)
{
    const auto cut = clipSlices (engine, clipIndex, per, preserveWarpedTiming);
    if (! cut.ok)
        return;
    auto notes = rack::sliceNotes (cut.slices, cut.to, cut.tickOf, 0);
    auto drum = rack::sliceToDrumRack (cut.samplePath, cut.slices, tr ("Slice").toStdString(), oneShot ? rack::SliceTemplate::oneShot : rack::SliceTemplate::gated,
                                       cut.to, cut.name);
    drum.colour = cut.colour;
    const auto capped = (int) cut.slices.size() > rack::maxSlicePads;
    auto added = -1;
    engine.editSong ([&] (Song& s) {
        added = rack::addSliceTrack (s, clipIndex, std::move (drum), std::move (notes), cut.name);
        if (added >= 0)
            s.currentPattern = (int) s.patterns.size() - 1;
    });
    if (capped && added >= 0)
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, tr ("Slice to New MIDI Track"),
                                                tr ("Only the first {0} slices got a pad.", { juce::String (rack::maxSlicePads) }));
    if (done)
        done();
}

// Live's line under "Create one slice per": "The current clip region is 64 beats long - this will result in 128 slices."
juce::String sliceCountLine (const ClipSlices& cut)
{
    const auto beats = std::abs (cut.beats - std::round (cut.beats)) < 0.005 ? juce::String (juce::roundToInt (cut.beats)) : juce::String (cut.beats, 2);
    return tr ("The current clip region is {0} beats long - this will result in {1} slices.", { beats, juce::String ((int) cut.slices.size()) });
}

// The dialog's own rows: the count line and the Preserve warped timing switch (an AlertWindow holds them, not owns them)
struct SliceDialogExtras
{
    juce::Label count;
    juce::Label preserveLabel;
    juce::TextButton preserve;
    juce::Component preserveRow;
};

juce::AlertWindow* buildSliceDialog (AudioEngine& engine, juce::Component* owner, int clipIndex, const std::shared_ptr<SliceDialogExtras>& extras)
{
    auto* box = new juce::AlertWindow (tr ("Slice to New MIDI track"), {}, juce::MessageBoxIconType::NoIcon, owner);
    const juce::StringArray per { tr ("Warp Marker"), tr ("Transient"), tr ("Bar"), tr ("1/2 Note"), tr ("1/4 Note"), tr ("1/8 Note"), tr ("1/16 Note"), tr ("1/32 Note") };
    box->addComboBox ("per", per, tr ("Create one slice per:"));
    box->getComboBoxComponent ("per")->setSelectedItemIndex ((int) rack::SlicePer::transient, juce::dontSendNotification);
    extras->count.setJustificationType (juce::Justification::centredLeft);
    extras->count.setSize (380, 40);
    extras->count.setMinimumHorizontalScale (0.8f);
    box->addCustomComponent (&extras->count);
    box->addComboBox ("preset", { tr ("Default"), tr ("One-shot") }, tr ("Slicing Preset:"));
    extras->preserveLabel.setText (tr ("Preserve warped timing:"), juce::dontSendNotification);
    extras->preserve.setClickingTogglesState (true);
    extras->preserve.setToggleState (true, juce::dontSendNotification);
    extras->preserve.setButtonText (tr ("On"));
    extras->preserveRow.addAndMakeVisible (extras->preserveLabel);
    extras->preserveRow.addAndMakeVisible (extras->preserve);
    extras->preserveRow.setSize (380, 26);
    extras->preserveLabel.setBounds (0, 2, 300, 22);
    extras->preserve.setBounds (310, 2, 60, 22);
    box->addCustomComponent (&extras->preserveRow);
    const auto update = [box, &engine, clipIndex, x = extras.get()] { // (raw: whoever holds the dialog keeps the extras)
        const auto cut = clipSlices (engine, clipIndex, (rack::SlicePer) std::clamp (box->getComboBoxComponent ("per")->getSelectedItemIndex(), 0, rack::slicePerCount - 1),
                                     x->preserve.getToggleState());
        x->count.setText (cut.ok ? sliceCountLine (cut) : juce::String(), juce::dontSendNotification);
        x->preserve.setButtonText (x->preserve.getToggleState() ? tr ("On") : tr ("Off"));
    };
    box->getComboBoxComponent ("per")->onChange = update;
    extras->preserve.onClick = update;
    update();
    box->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    box->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    return box;
}

void showSliceToNewTrackDialog (AudioEngine& engine, juce::Component& owner, int clipIndex, std::function<void()> done)
{
    auto extras = std::make_shared<SliceDialogExtras>();
    auto* box = buildSliceDialog (engine, &owner, clipIndex, extras);
    box->enterModalState (true, juce::ModalCallbackFunction::create ([box, &engine, clipIndex, done, extras] (int result) {
                              if (result != 1)
                                  return;
                              const auto per = (rack::SlicePer) std::clamp (box->getComboBoxComponent ("per")->getSelectedItemIndex(), 0, rack::slicePerCount - 1);
                              const auto oneShot = box->getComboBoxComponent ("preset")->getSelectedItemIndex() == 1;
                              sliceClipToNewTrack (engine, clipIndex, per, oneShot, extras->preserve.getToggleState(), done);
                          }),
                          true);
}
} // namespace

std::unique_ptr<juce::Component> makeSliceDialogForSnapshot (AudioEngine& engine, int clipIndex, int per)
{
    struct Holder final : juce::Component
    {
        std::shared_ptr<SliceDialogExtras> extras = std::make_shared<SliceDialogExtras>();
        std::unique_ptr<juce::AlertWindow> box;
        ~Holder() override { box.reset(); }
    };
    auto holder = std::make_unique<Holder>();
    holder->box.reset (buildSliceDialog (engine, nullptr, clipIndex, holder->extras));
    holder->box->getComboBoxComponent ("per")->setSelectedItemIndex (std::clamp (per, 0, rack::slicePerCount - 1), juce::sendNotificationSync);
    holder->addAndMakeVisible (*holder->box);
    holder->box->setTopLeftPosition (0, 0);
    holder->setSize (holder->box->getWidth(), holder->box->getHeight());
    return holder;
}

juce::PopupMenu PlaylistComponent::buildClipMenu (int index, int mouseX)
{
    const auto& song = engine.getSong();
    juce::PopupMenu menu;
    if (index < 0 || index >= (int) song.playlist.size())
        return menu;

    const auto clip = song.playlist[(std::size_t) index];
    // FL's audio clip menu has its own layout (two columns, FL's sections; round 190dz)
    if (clip.kind == ClipKind::audio && clip.channel >= 0 && clip.channel < (int) song.channels.size())
        return buildAudioClipMenu (index, mouseX);
    const auto targets = isSelected (index) ? selection : std::vector<int> { index };
    const auto many = targets.size() > 1;

    // --- the source: name, colour, uniqueness -------------------------------------------------------
    juce::String sourceName;
    if (clip.kind == ClipKind::pattern && clip.pattern >= 0 && clip.pattern < (int) song.patterns.size())
        sourceName = juce::String::fromUTF8 (song.patterns[(std::size_t) clip.pattern].name.c_str());
    else if (clip.kind == ClipKind::audio && clip.channel >= 0 && clip.channel < (int) song.channels.size())
        sourceName = juce::String::fromUTF8 (song.channels[(std::size_t) clip.channel].name.c_str());
    else if (clip.kind == ClipKind::automation && clip.automation >= 0 && clip.automation < (int) song.automations.size())
        sourceName = juce::String::fromUTF8 (song.automations[(std::size_t) clip.automation].name.c_str());
    {
        // the title band in the clip's colour
        auto colour = theme::colour::automation;
        auto kind = tr ("Automation clip");
        if (clip.kind == ClipKind::pattern)
        {
            colour = patternColour (song, clip.pattern);
            kind = tr ("Pattern clip");
        }
        else if (clip.kind == ClipKind::audio && clip.channel >= 0 && clip.channel < (int) song.channels.size())
        {
            colour = audioClipColour (song.channels[(std::size_t) clip.channel]);
            kind = tr ("Audio clip");
        }
        ui::addItemHeader (menu, many ? kind + " (" + juce::String ((int) targets.size()) + ")" : kind, sourceName, colour);
    }
    // (an audio clip's menu: buildAudioClipMenu, FL's own layout)
    menu.addItem (tr ("Properties..."), [this, index, targets] {
        showClipProperties (engine, index, targets, [safe = juce::Component::SafePointer<PlaylistComponent> (this)] {
            if (safe != nullptr)
                safe->sortClips();
        });
    });

    // (an audio clip's "Follow the project key" / "Loop key", round 190ds: in buildAudioClipMenu's LYNX items)
    if (clip.kind == ClipKind::pattern && onRenamePattern)
        menu.addItem (theme::mnemonic (tr ("Rename and color..."), 'R'), [this, p = clip.pattern] { onRenamePattern (p); });

    // FL's "Change color..." / "Random color" (the clip's source: its pattern, or its audio channel)
    if (clip.kind != ClipKind::automation)
    {
        auto setColour = [this, kind = clip.kind, src = clip.kind == ClipKind::pattern ? clip.pattern : clip.channel] (std::uint32_t colour) {
            engine.editSong ([kind, src, colour] (Song& s) {
                if (kind == ClipKind::pattern && src >= 0 && src < (int) s.patterns.size())
                    s.patterns[(std::size_t) src].colour = colour;
                else if (kind == ClipKind::audio && src >= 0 && src < (int) s.channels.size())
                    s.channels[(std::size_t) src].colour = colour;
            });
        };
        const auto current = clip.kind == ClipKind::pattern ? (clip.pattern >= 0 && clip.pattern < (int) song.patterns.size() ? song.patterns[(std::size_t) clip.pattern].colour : 0u)
                                                            : (clip.channel >= 0 && clip.channel < (int) song.channels.size() ? song.channels[(std::size_t) clip.channel].colour : 0u);
        menu.addSubMenu (tr ("Change color"), ui::colourMenu (current, setColour, juce::Rectangle<int> (juce::Desktop::getMousePosition().x, juce::Desktop::getMousePosition().y, 1, 1)));
        menu.addItem (tr ("Random color"), [setColour] {
            const auto& palette = lynxPalette();
            setColour (palette[(std::size_t) juce::Random::getSystemRandom().nextInt ((int) palette.size())]);
        });
    }
    menu.addSeparator();

    // FL's "Select source pattern / channel": the clip plays another source (patterns; audio clip channels; automations)
    {
        juce::PopupMenu sources;
        if (clip.kind == ClipKind::pattern)
            for (int p = 0; p < (int) song.patterns.size(); ++p)
                sources.addItem (juce::String::fromUTF8 (song.patterns[(std::size_t) p].name.c_str()), true, p == clip.pattern,
                                 [this, targets, p] { engine.editSong ([targets, p] (Song& s) { clipops::setClipSource (s, targets, ClipKind::pattern, p); }); });
        else if (clip.kind == ClipKind::automation)
            for (int a = 0; a < (int) song.automations.size(); ++a)
                sources.addItem (juce::String::fromUTF8 (song.automations[(std::size_t) a].name.c_str()), true, a == clip.automation,
                                 [this, targets, a] { engine.editSong ([targets, a] (Song& s) { clipops::setClipSource (s, targets, ClipKind::automation, a); }); });
        menu.addSubMenu (clip.kind == ClipKind::pattern ? tr ("Select source pattern") : tr ("Select source channel"), sources, sources.getNumItems() > 1);
    }

    if (clip.kind == ClipKind::pattern)
        menu.addItem (tr ("Make unique") + "\tShift+V+drag", [this, targets] {
            engine.editSong ([targets] (Song& s) { polish::makeClipsUnique (s, targets); });
        });
    else if (clip.kind == ClipKind::automation)
        menu.addItem (tr ("Make unique"), [this, index] {
            engine.editSong ([index] (Song& s) {
                auto& c = s.playlist[(std::size_t) index];
                auto copy = s.automations[(std::size_t) c.automation];
                copy.name += " #2";
                s.automations.push_back (std::move (copy));
                c.automation = (int) s.automations.size() - 1;
            });
        });
    // round 177b: with lanes on, an automation clip can become part of the lane of its target
    if (clip.kind == ClipKind::automation && autolanes::shown() && clip.automation >= 0 && clip.automation < (int) song.automations.size()
        && song.automations[(std::size_t) clip.automation].target.kind != AutomationTargetKind::none)
        menu.addItem (tr ("Move into lane"), [this, index] {
            setSelection ({});
            engine.editSong ([index] (Song& s) {
                if (index < (int) s.playlist.size() && s.playlist[(std::size_t) index].automation >= 0)
                    autolanes::clipToLane (s, index, autolanes::trackFor (s, s.automations[(std::size_t) s.playlist[(std::size_t) index].automation].target));
            });
        });

    menu.addItem (tr ("Select all clips of this source"), [this, index] { setSelection (clipops::sameSource (engine.getSong(), index)); });
    menu.addSeparator();

    // --- cutting and joining -------------------------------------------------------------------------
    const auto splitAt = snap (xToTick (mouseX) + gridTicks() / 2.0, true);
    menu.addItem (tr ("Split here"), splitAt > clip.start && splitAt < clip.end(), false, [this, index, splitAt] {
        setSelection ({});
        engine.editSong ([index, splitAt] (Song& s) {
            if (index < (int) s.playlist.size() && splitAt > s.playlist[(std::size_t) index].start && splitAt < s.playlist[(std::size_t) index].end())
                s.playlist.push_back (splitClip (s.playlist[(std::size_t) index], splitAt));
        });
        sortClips();
    });
    {
        // FL's "Chop" submenu: to the grid, or in bars / beats / parts of a beat
        juce::PopupMenu chop;
        auto chopBy = [this, targets] (std::int64_t grid) {
            setSelection ({});
            engine.editSong ([targets, grid] (Song& s) {
                for (auto i : targets) // chopping adds clips at the end: indices below stay valid
                    noteedit::chopClip (s.playlist, i, grid);
            });
            sortClips();
        };
        chop.addItem (tr ("Chop to grid"), [chopBy, grid = gridTicks()] { chopBy (grid); });
        chop.addSeparator();
        const auto bar = (std::int64_t) ppq * juce::jmax (1, song.beatsPerBar);
        const std::pair<const char*, std::int64_t> sizes[] = { { TR_NOOP ("Chop in bars"), bar },
                                                               { TR_NOOP ("Chop in beats"), (std::int64_t) ppq },
                                                               { TR_NOOP ("Chop in half beats"), (std::int64_t) ppq / 2 },
                                                               { TR_NOOP ("Chop in quarter beats"), (std::int64_t) ppq / 4 },
                                                               { TR_NOOP ("Chop in 8th beats"), (std::int64_t) ppq / 8 } };
        for (const auto& [label, ticks] : sizes)
            chop.addItem (tr (label), [chopBy, t = ticks] { chopBy (t); });
        menu.addSubMenu (tr ("Chop"), chop);
    }
    menu.addItem (tr ("Merge clips"), clipops::canMerge (song, targets), false, [this, targets] {
        setSelection ({});
        engine.editSong ([targets] (Song& s) { clipops::mergeClips (s, targets); });
        sortClips();
    });

    // --- pattern -------------------------------------------------------------------------------------
    if (clip.kind == ClipKind::pattern)
    {
        menu.addSeparator();
        menu.addItem (tr ("Open in Channel Rack"), [this, p = clip.pattern] {
            engine.editSong ([p] (Song& s) { if (p < (int) s.patterns.size()) s.currentPattern = p; }, false);
            if (onOpenPattern)
                onOpenPattern();
        });
        menu.addItem (tr ("Open in Piano Roll"), [this, p = clip.pattern] {
            engine.editSong ([p] (Song& s) { if (p < (int) s.patterns.size()) s.currentPattern = p; }, false);
            if (onOpenPianoRoll)
                onOpenPianoRoll();
        });
    }

    // --- automation ----------------------------------------------------------------------------------
    if (clip.kind == ClipKind::automation && clip.automation >= 0 && clip.automation < (int) song.automations.size())
    {
        menu.addSeparator();
        menu.addItem (tr ("Edit points..."), [this, a = clip.automation] { openAutomationEditor (engine, a); });
        menu.addItem (tr ("Delete automation (all of its clips)"), [this, a = clip.automation] {
            setSelection ({});
            engine.editSong ([a] (Song& s) { s.removeAutomation (a); });
        });
    }

    // --- mute and the clipboard ----------------------------------------------------------------------
    menu.addSeparator();
    menu.addItem (clip.muted ? tr ("Unmute") : tr ("Mute"), [this, targets, m = ! clip.muted] {
        engine.editSong ([targets, m] (Song& s) {
            for (auto i : targets)
                if (i < (int) s.playlist.size())
                    s.playlist[(std::size_t) i].muted = m;
        });
    });
    menu.addItem (theme::mnemonic (tr ("Cut"), 't') + "\tCtrl+X", [this, targets] {
        setSelection (targets);
        copySelection();
        deleteSelection();
    });
    menu.addItem (theme::mnemonic (tr ("Copy"), 'C') + "\tCtrl+C", [this, targets] {
        setSelection (targets);
        copySelection();
    });
    menu.addItem (theme::mnemonic (tr ("Paste here"), 'P') + "\tCtrl+V", ! clipboard.empty(), false, [this, at = snap (xToTick (mouseX), true)] { pasteClips (at); });
    menu.addItem (theme::mnemonic (many ? tr ("Delete selected") : tr ("Delete"), 'D') + "\tDel", [this, targets] {
        setSelection (targets);
        deleteSelection();
    });

    return menu;
}

// FL Studio 2026's audio clip menu (round 190dz; the model, FL's evidence and what LYNX leaves out: core/FlClipMenu.h):
// two columns — "Audio clip" and "Region" on the left, "Sample", "Automation" and "Fades" on the right — in FL's order
// and with FL's labels, then LYNX's own items (no FL counterpart) below a line at the end.
juce::PopupMenu PlaylistComponent::buildAudioClipMenu (int index, int mouseX)
{
    const auto& song = engine.getSong();
    juce::PopupMenu menu;
    const auto clip = song.playlist[(std::size_t) index];
    const auto targets = isSelected (index) ? selection : std::vector<int> { index };
    const auto many = targets.size() > 1;
    const auto ch = clip.channel;
    const auto& channel = song.channels[(std::size_t) ch];
    const auto path = juce::String::fromUTF8 (channel.samplePath.c_str());
    const auto sample = engine.getSampleLibrary().load (path);
    const auto span = sample != nullptr ? clipops::clipSpan (channel, sample) : precompute::PlayedSpan {};
    // the frames the clip plays (its Trim; round 180d), as the drawing and the playback count them
    const auto frames = sample != nullptr ? (double) span.frames : 0.0;
    const auto rate = sample != nullptr ? sample->sampleRate : 44100.0;
    const auto mouse = juce::Rectangle<int> (juce::Desktop::getMousePosition().x, juce::Desktop::getMousePosition().y, 1, 1);
    const auto label = [] (const flclipmenu::Entry& e) { return e.mnemonic != 0 ? theme::mnemonic (tr (e.text.c_str()), e.mnemonic) : tr (e.text.c_str()); };
    auto setColour = [this, ch] (std::uint32_t colour) {
        engine.editSong ([ch, colour] (Song& s) {
            if (ch < (int) s.channels.size())
                s.channels[(std::size_t) ch].colour = colour;
        });
    };

    // FL's Crossfade with › lists the other channels' audio clips that overlap this one in time (EEAUMenuPopup ->
    // FUN_01236f10); with none it is greyed. LYNX has no crossfade-with yet: shown only in that greyed state.
    const auto overlapped = std::any_of (song.playlist.begin(), song.playlist.end(), [&clip] (const Clip& c) {
        return c.kind == ClipKind::audio && c.channel != clip.channel && c.start < clip.end() && clip.start < c.end();
    });

    for (const auto& e : flclipmenu::shown (! overlapped, clipops::showCrossfadeSelected (song.playlist, index, selection)))
    {
        using K = flclipmenu::Entry::Kind;
        const auto& t = e.text;
        if (e.kind == K::separator)
            menu.addSeparator();
        else if (e.kind == K::columnBreak)
            menu.addColumnBreak();
        else if (e.kind == K::header && t == "Audio clip")
            ui::addItemHeader (menu, many ? tr ("Audio clip") + " (" + juce::String ((int) targets.size()) + ")" : tr ("Audio clip"),
                               juce::String::fromUTF8 (channel.name.c_str()), audioClipColour (channel));
        else if (e.kind == K::header)
            menu.addSectionHeader (tr (t.c_str()));
        else if (t == "Preview")
            menu.addItem (label (e), [this, index] { previewClipAudio (index); });
        else if (t == "Muted")
            menu.addItem (label (e), true, clip.muted, [this, targets, m = ! clip.muted] {
                engine.editSong ([targets, m] (Song& s) {
                    for (auto i : targets)
                        if (i < (int) s.playlist.size())
                            s.playlist[(std::size_t) i].muted = m;
                });
            });
        else if (t == "Rename and color...")
            menu.addItem (label (e), [this, ch] {
                const auto& c = engine.getSong().channels[(std::size_t) ch];
                showNameAndColourDialog (tr ("Rename audio clip"), juce::String::fromUTF8 (c.name.c_str()), c.colour,
                                         [this, ch] (const juce::String& name, std::uint32_t colour) {
                                             if (ch < (int) engine.getSong().channels.size())
                                                 engine.editSong ([ch, n = name.toStdString(), colour] (Song& s) {
                                                     s.channels[(std::size_t) ch].name = n;
                                                     s.channels[(std::size_t) ch].colour = colour;
                                                 });
                                         });
            });
        else if (t == "Change color...") // (FL: the name dialog with its colour selector, EEAUSetNameMenuClick flag 8)
            menu.addItem (label (e), [setColour, mouse, current = channel.colour] {
                ui::showColourPickerPopup (mouse, tr ("Change color..."), current != 0 ? juce::Colour (current) : juce::Colour (lynxPalette()[0]), {},
                                           [setColour] (juce::Colour c) { setColour (c.getARGB()); }, {}, [setColour] { setColour (0); });
            });
        else if (t == "Random color")
            menu.addItem (label (e), [setColour] {
                const auto& palette = lynxPalette();
                setColour (palette[(std::size_t) juce::Random::getSystemRandom().nextInt ((int) palette.size())]);
            });
        else if (t == "Change icon...") // (FL: the clip channel's icon, EEAUSetNameMenuClick flag 2; LYNX's own icon set)
            menu.addItem (label (e), [this, ch, mouse, current = channel.icon] {
                ui::showTrackIconPicker (mouse, current, [this, ch] (int icon) {
                    if (ch < (int) engine.getSong().channels.size())
                        engine.editSong ([ch, icon] (Song& s) { s.channels[(std::size_t) ch].icon = icon; });
                });
            });
        else if (t == "Select source channel")
        {
            juce::PopupMenu sources;
            for (int c = 0; c < (int) song.channels.size(); ++c)
                if (song.channels[(std::size_t) c].instrument == InstrumentKind::audioClip)
                    sources.addItem (juce::String::fromUTF8 (song.channels[(std::size_t) c].name.c_str()), true, c == ch,
                                     [this, targets, c] { engine.editSong ([targets, c] (Song& s) { clipops::setClipSource (s, targets, ClipKind::audio, c); }); });
            menu.addSubMenu (label (e), sources, sources.getNumItems() > 1);
        }
        else if (t == "Channel settings")
            menu.addItem (label (e), [this, ch] { openChannelSettings (engine, ch); });
        else if (t == "Make unique")
            menu.addItem (label (e), [this, targets] { engine.editSong ([targets] (Song& s) { clipops::makeAudioUnique (s, targets); }); });
        else if (t == "Select all similar clips")
            menu.addItem (label (e), [this, index] { setSelection (clipops::sameSource (engine.getSong(), index)); });
        else if (t == "Delete")
            menu.addItem (label (e), [this, targets] {
                setSelection (targets);
                deleteSelection();
            });
        else if (t == "Properties...")
            menu.addItem (label (e), [this, index, targets] {
                showClipProperties (engine, index, targets, [safe = juce::Component::SafePointer<PlaylistComponent> (this)] {
                    if (safe != nullptr)
                        safe->sortClips();
                });
            });
        else if (t == "Select region")
        {
            // "Full" (FL's Default radio item) and the file's regions (its cue markers): the clip plays that part
            juce::PopupMenu regions;
            const auto perTick = sample != nullptr ? clipops::framesPerTick (channel, clip, frames, rate, song.bpm) : 0.0;
            std::vector<flclipmenu::ClipWindow> windows;
            if (sample != nullptr)
                for (const auto& r : flclipmenu::sampleRegions (sample->markerStarts, (std::int64_t) sample->frames()))
                    if (const auto w = flclipmenu::regionWindow (r, span.first, span.frames, perTick))
                        windows.push_back (*w);
            const auto current = flclipmenu::currentRegion (windows, clip);
            auto setWindow = [this, targets] (flclipmenu::ClipWindow w) {
                engine.editSong ([targets, w] (Song& s) {
                    for (auto i : targets)
                        if (i < (int) s.playlist.size() && s.playlist[(std::size_t) i].kind == ClipKind::audio)
                        {
                            s.playlist[(std::size_t) i].offset = w.offset;
                            s.playlist[(std::size_t) i].offsetFraction = 0.0;
                            s.playlist[(std::size_t) i].length = w.length;
                        }
                });
                sortClips();
            };
            // (FL's radio check, FUN_00ffa400: "Full" while the clip has no start / end of its own — it plays its source
            // from the start to the end, however long the clip is drawn — a region when it plays exactly that region,
            // none otherwise)
            const auto full = flclipmenu::fullWindow (span.frames, perTick);
            const auto isFull = current < 0 && clip.offset == 0 && clip.length >= full.length - 1;
            regions.addItem (tr ("Full"), sample != nullptr, isFull, [setWindow, full] { setWindow (full); });
            for (int r = 0; r < (int) windows.size(); ++r)
                regions.addItem (tr ("Region {0}", { juce::String (r + 1) }), true, r == current, [setWindow, w = windows[(std::size_t) r]] { setWindow (w); });
            menu.addSubMenu (label (e), regions);
        }
        else if (t == "Chop")
        {
            // FL's Chop › "Time based" group (FUN_01236260), then LYNX's chop to the grid
            juce::PopupMenu chop;
            auto chopBy = [this, targets] (std::int64_t grid) {
                setSelection ({});
                engine.editSong ([targets, grid] (Song& s) {
                    for (auto i : targets) // chopping adds clips at the end: indices below stay valid
                        noteedit::chopClip (s.playlist, i, grid);
                });
                sortClips();
            };
            chop.addSectionHeader (tr ("Time based"));
            const auto bar = (std::int64_t) ppq * juce::jmax (1, song.beatsPerBar);
            for (const auto& c : flclipmenu::timeBasedChops())
                chop.addItem (tr (c.text), [chopBy, ticks = c.beatDivisor == 0 ? bar : (std::int64_t) ppq / c.beatDivisor] { chopBy (ticks); });
            chop.addSeparator();
            chop.addItem (tr ("Chop to grid"), [chopBy, grid = gridTicks()] { chopBy (grid); });
            menu.addSubMenu (label (e), chop);
        }
        else if (t == "Make unique as sample") // (round 189g)
            menu.addItem (label (e), [this, targets] {
                setSelection (targets);
                makeSelectionUniqueAsSample();
            });
        else if (t == "Edit sample") // (FL: the clip's channel sample into its editor, EEAUEditSampleMenuClick)
            menu.addItem (label (e), sample != nullptr, false, [this, ch, index] { openAudioEditorForChannel (engine, ch, index); });
        else if (t == "Extract stems from sample")
            menu.addItem (label (e), sample != nullptr, false, [this, index] { extractStemsFromClip (engine, index, [this] { sortClips(); }); });
        else if (t == "Detect tempo") // (live check 190ew PL-8: one item opening FL's Tempo detection dialog)
            menu.addItem (label (e), sample != nullptr, false, [this, index, sample] { showDetectTempo (index, sample); });
        else if (t == "Fit to tempo")
            menu.addSubMenu (label (e), fitToTempoMenu (index, sample), sample != nullptr);
        else if (t == "Automate")
        {
            // EEAUAutoMenu (round 190dx, core clipops::automateAudioClip): an automation clip of the clip channel's Levels
            // adjustment pan / volume (round 190dz) where the audio clip is (same track, start and length)
            juce::PopupMenu automate;
            const auto add = [this, index, &automate] (const juce::String& text, clipops::AutomateWhat what) {
                automate.addItem (text, [this, index, what] {
                    const auto target = clipops::automateTarget (engine.getSong(), index, what);
                    if (target.kind == AutomationTargetKind::none)
                        return;
                    const auto name = clipops::automateName (engine.getSong(), target, describeAutomationTarget (engine, target).toStdString());
                    int made = -1;
                    engine.editSong ([index, what, &name, &made] (Song& s) { made = clipops::automateAudioClip (s, index, what, name); });
                    sortClips();
                    // (live check 190ew PL-1: the new clip becomes the Playlist's current clip, as the picker shows)
                    if (made >= 0)
                    {
                        brush = { ClipKind::automation, made };
                        engine.sendChangeMessage();
                    }
                });
            };
            add (theme::mnemonic (tr ("Panning"), 'P'), clipops::AutomateWhat::panning);
            add (theme::mnemonic (tr ("Volume"), 'V'), clipops::AutomateWhat::volume); // (FL's default item)
            menu.addSubMenu (label (e), automate);
        }
        else if (t == "Crossfade with")
            menu.addSubMenu (label (e), juce::PopupMenu(), false);
        else if (t == "Reset fades") // (FL: "Reset fade times to 0"; Tag 1 into the fade handle's reset, FUN_00d01bc0 ->
                                         // FUN_00cfb460, as the handle's Reset fade in / out: the times and their tension)
            menu.addItem (label (e), [this, targets] {
                engine.editSong ([targets] (Song& s) {
                    for (auto i : targets)
                        if (i < (int) s.playlist.size())
                        {
                            auto& c = s.playlist[(std::size_t) i];
                            c.fadeIn = c.fadeOut = 0;
                            c.fadeInTension = c.fadeOutTension = 0.0f;
                        }
                });
            });
        else if (t == "Fade in curve" || t == "Fade out curve") // (live check 190ew PL-7, core flclipmenu::fadeCurve)
            menu.addSubMenu (label (e), fadeHandleMenu (index, t == "Fade in curve" ? 0 : 1, true));
        else if (t == "Crossfade selected") // (round 189o: the same Create crossfade as the fade handle menu)
            menu.addItem (label (e), [this, index, chosen = selection] {
                engine.editSong ([index, chosen] (Song& s) { clipops::createCrossfades (s.playlist, index, 0, chosen); });
                repaint();
            });
    }

    // --- LYNX's own items (no FL counterpart), core flclipmenu::lynxExtras --------------------------------------------
    menu.addSeparator();
    // round 190ds, Simple workflow only (LYNX's own): the clip follows the project key, and its loop's key
    if (simpleHeaders)
        addKeyFollowItems (menu, index, targets);
    // comping (round 163): a take lane's clip onto its comp track (the group header above)
    if (const auto comp = takes::compTrackOf (song, clip.track); comp >= 0)
    {
        // the loop region where it covers the clip (comping a phrase), else the whole clip
        const auto useLoop = song.hasLoop() && song.loopStart < clip.end() && song.loopEnd > clip.start;
        const auto from = useLoop ? std::max (song.loopStart, clip.start) : clip.start;
        const auto to = useLoop ? std::min (song.loopEnd, clip.end()) : clip.end();
        menu.addItem (useLoop ? tr ("Promote this take in the loop region to the comp track") : tr ("Promote this take to the comp track"), [this, index, comp, from, to] {
            setSelection ({});
            engine.editSong ([index, comp, from, to] (Song& s) { takes::promoteTake (s, index, comp, from, to); });
            sortClips();
        });
    }
    menu.addSubMenu (tr ("Color palette"), ui::colourMenu (channel.colour, setColour, mouse));
    const auto splitAt = snap (xToTick (mouseX) + gridTicks() / 2.0, true);
    menu.addItem (tr ("Split here"), splitAt > clip.start && splitAt < clip.end(), false, [this, index, splitAt] {
        setSelection ({});
        engine.editSong ([index, splitAt] (Song& s) {
            if (index < (int) s.playlist.size() && splitAt > s.playlist[(std::size_t) index].start && splitAt < s.playlist[(std::size_t) index].end())
                s.playlist.push_back (splitClip (s.playlist[(std::size_t) index], splitAt));
        });
        sortClips();
    });
    menu.addItem (tr ("Merge clips"), clipops::canMerge (song, targets), false, [this, targets] {
        setSelection ({});
        engine.editSong ([targets] (Song& s) { clipops::mergeClips (s, targets); });
        sortClips();
    });
    menu.addItem (tr ("Reverse"), sample != nullptr, channel.sampler.reverse, [this, index, frames, rate] {
        engine.editSong ([index, frames, rate] (Song& s) {
            const auto c = s.playlist[(std::size_t) index].channel;
            clipops::reverseAudio (s, index, clipops::fileTicks (s.channels[(std::size_t) c], frames, rate, s.bpm));
        });
    });
    // Live's Convert Melody / Harmony / Drums to New MIDI Track, the FL way: a new pattern on new channels (round a2m189b)
    {
        juce::PopupMenu convert;
        const std::pair<const char*, a2m::Mode> modes[] = { { TR_NOOP ("Melody to new pattern"), a2m::Mode::melody },
                                                            { TR_NOOP ("Harmony to new pattern"), a2m::Mode::harmony },
                                                            { TR_NOOP ("Drums to new pattern"), a2m::Mode::drums } };
        for (const auto& [text, mode] : modes)
            convert.addItem (tr (text), [this, index, m = mode] { convertClipToMidi (engine, index, m, [this] { sortClips(); }); });
        menu.addSubMenu (tr ("Convert to MIDI"), convert, sample != nullptr);
    }
    // Live's Slice to New MIDI Track (round 190ec): a Drum Rack with a pad per slice and a pattern that replays them
    menu.addItem (tr ("Slice to New MIDI Track..."), sample != nullptr && ! clipops::playsBackwards (channel, clip), false,
                  [this, index] { showSliceToNewTrackDialog (engine, *this, index, [this] { sortClips(); }); });
    menu.addItem (tr ("Pitch... (semitones, cents, keep length)"), sample != nullptr, false, [this, ch] { showSamplePitchDialog (engine, ch); });
    menu.addItem (tr ("Normalize"), [this, targets] { normalizeClips (targets); });
    {
        juce::PopupMenu gains;
        for (auto db : { 6.0f, 3.0f, 0.0f, -3.0f, -6.0f, -12.0f })
        {
            const auto g = juce::Decibels::decibelsToGain (db);
            gains.addItem ((db > 0 ? "+" : "") + juce::String (db, 0) + " dB", true, std::abs (clip.gain - g) < 0.01f, [this, targets, g] {
                engine.editSong ([targets, g] (Song& s) {
                    for (auto i : targets)
                        if (i < (int) s.playlist.size() && s.playlist[(std::size_t) i].kind == ClipKind::audio)
                            s.playlist[(std::size_t) i].gain = g;
                });
            });
        }
        menu.addSubMenu (tr ("Clip volume"), gains);
    }
    menu.addItem (tr ("Open in a new Sampler channel"), sample != nullptr, false, [this, ch] {
        if (onOpenInSampler)
            onOpenInSampler (ch);
    });
    menu.addItem (tr ("Show the file in Explorer"), juce::File (path).existsAsFile(), false, [path] { juce::File (path).revealToUser(); });
    menu.addItem (theme::mnemonic (tr ("Cut"), 't') + "\tCtrl+X", [this, targets] {
        setSelection (targets);
        copySelection();
        deleteSelection();
    });
    menu.addItem (theme::mnemonic (tr ("Copy"), 'C') + "\tCtrl+C", [this, targets] {
        setSelection (targets);
        copySelection();
    });
    menu.addItem (theme::mnemonic (tr ("Paste here"), 'P') + "\tCtrl+V", ! clipboard.empty(), false, [this, at = snap (xToTick (mouseX), true)] { pasteClips (at); });
    return menu;
}

// The "Fit to tempo" / "Detect tempo" fit of an audio clip's file to a number of beats (keeping its pitch or not)
std::function<void (float, bool)> PlaylistComponent::clipFitter (int index, std::shared_ptr<const SampleData> sample)
{
    const auto& song = engine.getSong();
    const auto& channel = song.channels[(std::size_t) song.playlist[(std::size_t) index].channel];
    // the frames the clip plays (its Trim; round 180d), as the drawing and the playback count them
    const auto frames = sample != nullptr ? (double) clipops::clipSpan (channel, sample).frames : 0.0;
    const auto rate = sample != nullptr ? sample->sampleRate : 44100.0;
    return [this, index, frames, rate] (float toBeats, bool keepPitch) {
        engine.editSong ([index, frames, rate, toBeats, keepPitch] (Song& s) {
            const auto ch = s.playlist[(std::size_t) index].channel;
            auto& channel = s.channels[(std::size_t) ch];
            auto& sampler = channel.sampler;
            const auto before = clipops::fileTicks (channel, frames, rate, s.bpm);
            sampler.stretchBeats = toBeats;
            // "Original speed" keeps a pitched clip's "keep length" (round 163)
            sampler.keepPitch = keepPitch && (toBeats > 0.0f || sampler.pitch != 0.0f || channel.misc.fineTune != 0.0f);
            // every clip of the file keeps showing the same audio: offsets and lengths scale with it
            clipops::rescaleClipsOf (s, ch, before, clipops::fileTicks (channel, frames, rate, s.bpm));
        });
        sortClips();
    };
}

// The audio clip menu's "Fit to tempo" submenu (round 190dz: out of buildClipMenu, unchanged)
juce::PopupMenu PlaylistComponent::fitToTempoMenu (int index, std::shared_ptr<const SampleData> sample)
{
    const auto& song = engine.getSong();
    const auto& channel = song.channels[(std::size_t) song.playlist[(std::size_t) index].channel];
    // stretching the channel's file to a whole number of beats at the project tempo
    juce::PopupMenu fit;
    const auto beats = tempodetect::fitBeatsOf (sample.get(), song.bpm);
    const auto fitTo = clipFitter (index, sample);
    if (beats > 0.0f)
    {
        const auto whole = std::abs (beats - std::round (beats)) < 0.005f;
        const auto label = juce::String (beats, whole ? 0 : 2) + " " + (beats == 1.0f ? tr ("beat") : tr ("beats"));
        fit.addItem (tr ("Fit to tempo, keep pitch ({0})", { label }), true, channel.sampler.stretchBeats == beats && channel.sampler.keepPitch,
                     [fitTo, beats] { fitTo (beats, true); });
        fit.addItem (tr ("Fit to tempo, resample ({0})", { label }), true, channel.sampler.stretchBeats == beats && ! channel.sampler.keepPitch,
                     [fitTo, beats] { fitTo (beats, false); });
        fit.addItem (tr ("Half as long"), channel.sampler.stretchBeats > 0.0f, false,
                     [fitTo, b = channel.sampler.stretchBeats, kp = channel.sampler.keepPitch] { fitTo (juce::jmax (0.25f, b * 0.5f), kp); });
        fit.addItem (tr ("Twice as long"), channel.sampler.stretchBeats > 0.0f, false,
                     [fitTo, b = channel.sampler.stretchBeats, kp = channel.sampler.keepPitch] { fitTo (juce::jmin (1024.0f, b * 2.0f), kp); });
        fit.addSeparator();
    }
    // FL's TIME presets (r168s c_f20): the whole file lasts exactly this long
    for (const auto preset : clipops::timePresetBeats (song.beatsPerBar))
    {
        const auto bars = song.beatsPerBar > 0 && preset >= (float) song.beatsPerBar ? preset / (float) song.beatsPerBar : 0.0f;
        const auto label = bars > 0.0f ? (bars == 1.0f ? tr ("1 bar") : tr ("{0} bars", { juce::String (bars, 0) }))
                                       : (preset == 1.0f ? tr ("1 beat") : tr ("{0} beats", { juce::String (preset, 0) }));
        fit.addItem (label, true, channel.sampler.stretchBeats == preset, [fitTo, preset, kp = channel.sampler.keepPitch] { fitTo (preset, kp); });
    }
    fit.addSeparator();
    fit.addItem (tr ("Original speed (no stretch)"), true, channel.sampler.stretchBeats <= 0.0f, [fitTo, kp = channel.sampler.keepPitch] { fitTo (0.0f, kp); });
    return fit;
}

// The audio clip menu's "Detect tempo" (round 190dz: out of buildClipMenu; round 190ey: FL's dialog)
void PlaylistComponent::showDetectTempo (int index, std::shared_ptr<const SampleData> sample)
{
    // FL's "Detect tempo" (live check 190ew PL-8; LYNX's own detector, core/TempoDetect): one dialog, "Tempo detection -
    // <clip>", whose choice becomes the sample's tempo: the clip's file fitted to that many beats (pitch kept), or no
    // tempo (core/TempoDialog.h)
    if (sample == nullptr || index < 0 || index >= (int) engine.getSong().playlist.size())
        return;
    ui::TempoDetectionRequest r;
    {
        const auto& song = engine.getSong();
        const auto ch = song.playlist[(std::size_t) index].channel;
        r.name = ch >= 0 && ch < (int) song.channels.size() ? juce::String::fromUTF8 (song.channels[(std::size_t) ch].name.c_str()) : juce::String();
    }
    r.sample = sample;
    r.projectBpm = engine.getSong().bpm;
    r.hasTempoInfo = sample->acidBeats > 0.0f;
    r.apply = [fitTo = clipFitter (index, sample)] (double beats) { fitTo ((float) beats, beats > 0.0); };
    ui::showTempoDetectionDialog (std::move (r));
}

void PlaylistComponent::normalizeClips (const std::vector<int>& targets, bool asGroup)
{
    // FL's Normalize (round 189n, clipops::normalizeGain / groupNormalizeGain): the clip gain that brings the peak of the
    // played part to 0 dB, each clip on its own (-96.1 ... +36 dB) or one gain for the whole group
    const auto& song = engine.getSong();
    std::vector<std::pair<int, float>> gainsFor;

    for (auto i : targets)
    {
        if (i < 0 || i >= (int) song.playlist.size())
            continue;
        const auto& c = song.playlist[(std::size_t) i];

        if (c.kind != ClipKind::audio || c.channel < 0 || c.channel >= (int) song.channels.size())
            continue;

        const auto& channel = song.channels[(std::size_t) c.channel];
        const auto sample = engine.getSampleLibrary().load (juce::String::fromUTF8 (channel.samplePath.c_str()));

        if (sample == nullptr || sample->frames() == 0)
            continue;

        const auto span = clipops::clipSpan (channel, sample); // what the clip plays of the file (round 180d)
        const auto total = (double) span.frames;
        const auto perTick = clipops::framesPerTick (channel, c, total, sample->sampleRate, song.bpm);
        auto from = juce::jlimit (0.0, total, (double) c.offset * perTick);
        auto to = juce::jlimit (from, total, (double) (c.offset + c.length) * perTick);
        if (clipops::playsBackwards (channel, c)) // the clip's offset counts from the played span's end
        {
            const auto f = total - to, t = total - from;
            from = f;
            to = t;
        }
        from += (double) span.first;
        to += (double) span.first;
        float peak = 0.0f;

        for (auto* data : { sample->leftData(), sample->rightData() })
            if (data != nullptr)
                for (auto f = (std::size_t) from; f < (std::size_t) to; ++f)
                    peak = juce::jmax (peak, std::abs (data[f]));

        gainsFor.push_back ({ i, peak });
    }

    if (asGroup)
    {
        std::vector<float> peaks;
        for (auto [i, peak] : gainsFor)
            peaks.push_back (peak);
        const auto g = clipops::groupNormalizeGain (peaks);
        if (g == 0.0f)
            return;
        for (auto& entry : gainsFor)
            entry.second = g;
    }
    else
        for (auto& [i, peak] : gainsFor)
            peak = clipops::normalizeGain (peak, song.playlist[(std::size_t) i].gain);

    if (! gainsFor.empty())
        engine.editSong ([gainsFor] (Song& s) {
            for (auto [i, g] : gainsFor)
                if (i < (int) s.playlist.size())
                    s.playlist[(std::size_t) i].gain = g;
        });
}

void PlaylistComponent::visibilityChanged()
{
    // hidden while a clip preview or the Listen tool is sounding (no mouse up will come here): stop it
    if (! isShowing() && (drag == Drag::preview || drag == Drag::listen))
    {
        if (drag == Drag::preview)
            stopPreview();
        else
            engine.stop();
        drag = Drag::none;
    }
}

void PlaylistComponent::startPreview (int clip, int x)
{
    const auto& song = engine.getSong();

    if (clip < 0 || clip >= (int) song.playlist.size())
        return;

    const auto c = song.playlist[(std::size_t) clip];
    previewPositionBefore = engine.getTransport().positionTicks.load();
    previewPatternBefore = -1;
    previewRestoreSong = false;
    drag = Drag::preview;
    const auto at = juce::jlimit ((double) c.start, (double) c.end(), xToTick (x));

    if (c.kind == ClipKind::pattern && c.pattern >= 0 && c.pattern < (int) song.patterns.size())
    {
        // a pattern clip plays its pattern alone, from the clicked point
        const auto length = (double) juce::jmax<std::int64_t> (1, song.patterns[(std::size_t) c.pattern].effectiveLength (song.beatsPerBar));
        previewRestoreSong = song.mode == PlayMode::song;
        previewPatternBefore = song.currentPattern;
        engine.editSong ([p = c.pattern] (Song& s) {
            s.mode = PlayMode::pattern;
            s.currentPattern = p;
        }, false);
        engine.seek (std::fmod (at - (double) c.start + (double) c.offset, length));
    }
    else
    {
        if (song.mode != PlayMode::song)
            engine.editSong ([] (Song& s) { s.mode = PlayMode::song; }, false);
        engine.seek (at);
    }

    engine.play();
    setMouseCursor (editorCursor (gesture::Cursor::listen));
    ui::postHint ({ tr ("Previewing the clip: release the mouse to stop"), {}, false, false }, 60000);
}

juce::PopupMenu PlaylistComponent::buildAddTrackMenu()
{
    // FL's AddInstrumentMenu (round 190dx [FORM]): "Audio track": Add audio track (FL's default item); "Instrument
    // track": More plugins..., a line, the generator list (FUN_01195420, the Channel rack's Insert list)
    juce::PopupMenu menu;
    menu.addSectionHeader (tr ("Audio track"));
    menu.addItem (tr ("Add audio track"), [this] { addAudioTrackNow(); });
    menu.addSectionHeader (tr ("Instrument track"));
    menu.addItem (tr ("More plugins..."), [this] { morePluginsForTrack (-1); });
    menu.addSeparator();
    quickpick::addGeneratorMenu (menu, engine, [this] (ChannelDef c) { instrumentForTrack (-1, std::move (c)); }, nullptr, nullptr, this);
    quickpick::addFolderViewItems (menu, engine);
    return menu;
}

int PlaylistComponent::addAudioTrackNow()
{
    int track = -1;
    engine.editSong ([&track] (Song& s) { track = trackmodes::addAudioTrack (s); });
    if (track < 0)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Can't create new audio track"),
                                                tr ("There are no available mixer tracks."));
        return -1;
    }
    selectTrack (track); // FL: the new track alone is selected, scrolled into view
    repaint();
    return track;
}

void PlaylistComponent::instrumentForTrack (int track, ChannelDef generator)
{
    const auto& song = engine.getSong();
    const auto kind = generator.instrument;
    int channel = -1;
    // FL (EEPLTrackMorePlugMenuClick): on an instrument track the chosen generator replaces its channel's
    if (track >= 0 && track < (int) song.playlistTracks.size() && song.playlistTracks[(std::size_t) track].mode == PlaylistTrackMode::instrument
        && song.playlistTracks[(std::size_t) track].channel >= 0 && song.playlistTracks[(std::size_t) track].channel < (int) song.channels.size())
    {
        channel = song.playlistTracks[(std::size_t) track].channel;
        bool ok = false;
        engine.editSong ([channel, &generator, &ok] (Song& s) { ok = replaceGenerator (s, channel, std::move (generator)); });
        if (! ok)
            return;
    }
    else
    {
        if (kind == InstrumentKind::audioClip || kind == InstrumentKind::controller)
        {
            // FL's error (LineAssignInstrTrackMenuClick, round 190dx)
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Error"),
                                                    tr ("This generator can't be used as an instrument track."));
            return;
        }
        engine.editSong ([&] (Song& s) { channel = trackmodes::addInstrumentTrack (s, std::move (generator), track); });
        if (channel < 0)
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Can't create new instrument track"),
                                                    tr ("There are no available mixer tracks."));
            return;
        }
        const auto tracks = trackmodes::instrumentTracksOf (engine.getSong(), channel);
        if (! tracks.empty())
            selectTrack (tracks.front());
    }
    repaint();
    if (onGeneratorAdded)
        onGeneratorAdded (channel, kind);
}

void PlaylistComponent::morePluginsForTrack (int track)
{
    // FL's More plugins...: its plugin dialog for generators (FUN_010dc760); LYNX's plugin picker
    juce::Component::SafePointer<PlaylistComponent> safe (this);
    showPluginPicker (
        engine, true,
        [safe, track] (PluginRef ref) {
            if (safe == nullptr)
                return;
            ChannelDef c;
            c.name = ref.name;
            c.instrument = InstrumentKind::plugin;
            c.plugin = ref;
            safe->instrumentForTrack (track, std::move (c));
        },
        [safe, track] (ChannelDef c) {
            if (safe != nullptr)
                safe->instrumentForTrack (track, std::move (c));
        });
}

void PlaylistComponent::previewClipAudio (int clip)
{
    // FL plays the clip through its channel as the clip plays it (offset, pitch, stretch; FUN_01239e40 → FUN_00ffa0a0).
    // LYNX: the clip's own audio as its channel's instrument renders it (renderClipAudio), at the clip's and the
    // channel's volume, through the preview output (the insert's effects are not heard)
    const auto& song = engine.getSong();
    if (clip < 0 || clip >= (int) song.playlist.size() || song.playlist[(std::size_t) clip].kind != ClipKind::audio)
        return;
    const auto& c = song.playlist[(std::size_t) clip];
    if (c.channel < 0 || c.channel >= (int) song.channels.size())
        return;
    // Round 190ey (live check 190ew PL-3): FL plays it through the channel's mixer insert (its effects and meter), to the
    // clip's end; transport start / stop ends it. An audio clip channel plays it so (the song's own note for the clip)
    const auto& channel = song.channels[(std::size_t) c.channel];
    if (channel.instrument == InstrumentKind::audioClip && engine.previewClipThroughChannel ((std::size_t) clip))
        return;
    // (else, as before: with the channel's Levels adjustment volume, round 190dz: FL's channel gain includes it)
    const auto gain = c.gain * channelGain (channel.volume) * levelAdjustGain (channel.misc.levels.volume);
    auto audio = engine.renderClipAudio ((std::size_t) clip);
    if (audio.left.empty())
        return;
    auto sample = std::make_shared<SampleData>();
    sample->sampleRate = audio.sampleRate;
    sample->left = std::move (audio.left);
    sample->right = std::move (audio.right);
    engine.previewSample (std::move (sample), false, gain);
}

void PlaylistComponent::stopPreview()
{
    engine.stop();

    if (previewPatternBefore >= 0)
        engine.editSong ([this] (Song& s) {
            if (previewPatternBefore < (int) s.patterns.size())
                s.currentPattern = previewPatternBefore;
            if (previewRestoreSong)
                s.mode = PlayMode::song;
        }, false);

    engine.seek (previewPositionBefore);
    previewPatternBefore = -1;
    ui::clearPostedHint();
}

void PlaylistComponent::setRipple (bool on)
{
    ripple = on;
    repaint();
}

void PlaylistComponent::rippleDeleteSelection()
{
    if (selection.empty())
        return;

    auto doomed = selection;
    setSelection ({});
    engine.editSong ([&doomed] (Song& s) { noteedit::rippleDelete (s.playlist, doomed); });
}

void PlaylistComponent::zoomAroundCentre (double factor)
{
    const auto grid = gridArea();
    const auto centre = xToTick (grid.getCentreX());
    pixelsPerTick = juce::jlimit (4.0 / (4 * ppq), 800.0 / (4 * ppq), pixelsPerTick * factor);
    scrollTicks = juce::jmax (0.0, centre - (grid.getCentreX() - headerWidth) / pixelsPerTick);
    updateScrollBars();
    repaintZoomed();
}

void PlaylistComponent::zoomToBars (int bars)
{
    const auto bar = (double) ppq * juce::jmax (1, engine.getSong().beatsPerBar);
    const auto from = std::floor (scrollTicks / bar) * bar;
    zoomToTicks (from, from + bars * bar);
}

void PlaylistComponent::zoomAll()
{
    zoomToTicks (0.0, (double) engine.getSong().songLength());
}

void PlaylistComponent::zoomToSelection()
{
    const auto& song = engine.getSong();

    if (song.hasLoop() && selection.empty())
        return zoomToTicks ((double) song.loopStart, (double) song.loopEnd);

    if (selection.empty())
        return zoomAll();

    std::int64_t from = INT64_MAX, to = 0;
    int top = 1 << 20;
    for (auto i : selection)
        if (i >= 0 && i < (int) song.playlist.size())
        {
            from = juce::jmin (from, song.playlist[(std::size_t) i].start);
            to = juce::jmax (to, song.playlist[(std::size_t) i].end());
            top = juce::jmin (top, song.playlist[(std::size_t) i].track);
        }

    zoomToTicks ((double) from, (double) to);
    scrollY = juce::jmax (0, trackToY (top) - tracksTop() + scrollY);
    updateScrollBars();
    repaint();
}

void PlaylistComponent::centreOnPlayhead()
{
    const auto tick = engine.songPlayheadTicks();
    scrollTicks = juce::jmax (0.0, tick - gridArea().getWidth() / pixelsPerTick / 2.0);
    updateScrollBars();
    repaint();
}

bool PlaylistComponent::runAction (const std::string& id)
{
    auto is = [&id] (const char* name) { return id == name; };

    if (is ("playlist.zoomIn")) zoomAroundCentre (1.25);
    else if (is ("playlist.zoomOut")) zoomAroundCentre (1.0 / 1.25);
    else if (is ("playlist.zoom1")) zoomToBars (1);
    else if (is ("playlist.zoom2")) zoomToBars (4);
    else if (is ("playlist.zoom3")) zoomToBars (16);
    else if (is ("playlist.zoomAll")) zoomAll();
    else if (is ("playlist.zoomSelection")) zoomToSelection();
    else if (is ("playlist.centrePlayhead")) centreOnPlayhead();
    else if (is ("playlist.rippleDelete")) rippleDeleteSelection();
    else if (is ("playlist.ripple")) setRipple (! ripple);
    else if (is ("playlist.autoCrossfades")) engine.editSong ([] (Song& s) { s.autoCrossfades = ! s.autoCrossfades; });
    else if (is ("playlist.patternBlocks")) togglePatternBlocks();
    else if (is ("playlist.automationLanes")) toggleAutomationLanes();
    else if (is ("playlist.blocksToClips")) turnBlocksIntoClips();
    else if (is ("playlist.follow")) EditorGlobals::get().setFollowing (false, ! EditorGlobals::get().isFollowing (false));
    else if (is ("playlist.selectTimeAround")) selectTimeAroundSelection();
    else if (is ("playlist.swipeSelectionToComp")) swipeTimeSelectionToComp (currentTrack);
    else if (is ("playlist.timePrevious")) moveTimeSelection (-1);
    else if (is ("playlist.timeNext")) moveTimeSelection (1);
    else if (is ("playlist.markerAdd")) addMarkerByKey();
    else if (is ("playlist.markerAuto")) addAutoMarker();
    else if (is ("playlist.markerSignature")) addSignatureChangeByKey();
    else if (is ("playlist.markerLoop")) placeLoopMarker();
    else if (is ("playlist.pitchClipsDown")) pitchSelectedAudioClips (-1);
    else if (is ("playlist.pitchClipsUp")) pitchSelectedAudioClips (1);
    else if (is ("playlist.reverseClips")) reverseSelectedAudioClips();
    else if (is ("playlist.quantizeStarts")) quantizeClips (1.0);
    else if (is ("playlist.quantizeHalf")) quantizeClips (0.5);
    else if (is ("playlist.performanceMode")) togglePerformanceMode();
    else if (is ("playlist.zoomPerformance")) zoomOnPerformanceClips();
    else if (is ("playlist.perfClipProgress")) { showClipProgress = ! showClipProgress; repaint(); }
    else if (is ("playlist.perfTrackProgress")) { showTrackProgress = ! showTrackProgress; repaint(); }
    else if (is ("playlist.muteClips") || is ("playlist.unmuteClips"))
    {
        // FL limits it to the time selection when there is one
        const auto& song = engine.getSong();
        const auto from = song.hasLoop() ? song.loopStart : (std::int64_t) 0;
        const auto to = song.hasLoop() ? song.loopEnd : std::numeric_limits<std::int64_t>::max();
        engine.editSong ([chosen = selection, mute = is ("playlist.muteClips"), from, to] (Song& s) { clipops::setClipsMuted (s, chosen, mute, from, to); });
    }
    else if (is ("playlist.mergePatternClips")) mergePatternClipsByKey();
    else if (is ("playlist.mergeSimilarClips")) mergeSimilarClipsByKey();
    else if (is ("playlist.mergeAutomationClips")) mergeAutomationClipsByKey();
    else if (is ("playlist.cloneTracks")) cloneTracksWithDialog (! selectedTracks.empty() ? selectedTracks : std::vector<int> { juce::jmax (0, currentTrack) });
    else if (is ("playlist.makeUnique")) makeSelectionUnique();
    else if (is ("playlist.stretchMode")) setStretchOnResize (! EditorGlobals::get().stretchOnResize);
    else if (is ("playlist.createVariants"))
    {
        EditorGlobals::get().createVariants = ! EditorGlobals::get().createVariants;
        if (EditorGlobals::get().onOptionsChanged)
            EditorGlobals::get().onOptionsChanged();
    }
    else if (is ("playlist.lockAllToContent")) lockAllTracks (true);
    else if (is ("playlist.unlockAllFromContent")) lockAllTracks (false);
    else if (is ("playlist.resetEmptyTracks")) resetEmptyTracksAsked();
    else if (is ("playlist.collapseAllGroups")) engine.editSong ([] (Song& s) { pltracks::setAllGroupsCollapsed (s, true); });
    else if (is ("playlist.expandAllGroups")) engine.editSong ([] (Song& s) { pltracks::setAllGroupsCollapsed (s, false); });
    else if (is ("playlist.shiftLeft")) shiftSelectedClips (-1, 0);
    else if (is ("playlist.shiftRight")) shiftSelectedClips (1, 0);
    else if (is ("playlist.shiftUp")) shiftSelectedClips (0, -1);
    else if (is ("playlist.shiftDown")) shiftSelectedClips (0, 1);
    else if (is ("playlist.nudgeLeft")) shiftSelectedClips (-1, 0, true);
    else if (is ("playlist.nudgeRight")) shiftSelectedClips (1, 0, true);
    else if (is ("playlist.sliceInsertSpace")) sliceInsertTimeSelection();
    else if (is ("playlist.trimSpace")) trimTimeSelection();
    else if (is ("playlist.loopSwitch")) setLoopSwitch (! engine.getSong().loopPlays());
    else if (is ("playlist.loopHalve") || is ("playlist.loopDouble"))
    {
        if (engine.getSong().hasLoop())
            engine.editSong ([factor = is ("playlist.loopHalve") ? 0.5 : 2.0] (Song& s) { arrange::scaleLoop (s, factor); }, false);
    }
    else if (is ("playlist.duplicateTime")) timeCommand (0);
    else if (is ("playlist.cutTime")) timeCommand (1);
    else if (is ("playlist.copyTime")) timeCommand (2);
    else if (is ("playlist.pasteTime")) timeCommand (3);
    else if (is ("playlist.cropClips")) timeCommand (4);
    else if (is ("playlist.automationFollowsClips"))
    {
        auto& g = EditorGlobals::get();
        g.automationFollowsClips = automationFollows() ? 0 : 1;
        if (g.onOptionsChanged)
            g.onOptionsChanged();
    }
    else if (is ("playlist.deselect"))
    {
        setSelection ({}); // (FL's EEDeselAction, FUN_00eb1a90: the clips and the time selection)
        if (engine.getSong().hasLoop())
            engine.editSong ([] (Song& s) { s.loopStart = s.loopEnd = 0; }, false);
    }
    else if (is ("playlist.selectBySource")) selectBySelectedSource();
    else if (is ("playlist.invertSelection")) setSelection (clipops::invertedSelection ((int) engine.getSong().playlist.size(), selection));
    else if (is ("playlist.leftResizing") || is ("playlist.wheelNudging") || is ("playlist.manualFades") || is ("playlist.snapFadeHandles")
             || is ("playlist.magicLasso"))
    {
        auto& g = EditorGlobals::get();
        auto& flag = is ("playlist.leftResizing") ? g.leftResizing : is ("playlist.wheelNudging") ? g.wheelNudging
                   : is ("playlist.manualFades")  ? g.manualFades   : is ("playlist.snapFadeHandles") ? g.snapFadeHandles : g.magicLasso;
        flag = ! flag;
        lassoPath.clear();
        if (g.onOptionsChanged)
            g.onOptionsChanged();
    }
    else if (is ("playlist.makeUniqueAsSample")) makeSelectionUniqueAsSample();
    else if (is ("playlist.purgeUnusedAudio"))
    {
        setSelection ({});
        engine.editSong ([] (Song& s) { pltracks::purgeUnusedAudioClips (s); });
        sortClips();
    }
    else
        return false;

    return true;
}

namespace
{
// FL's Clone window (FUN_012edbe0 [DECOMP]: TaskDialog "Clone" / "What do you want to clone along with the track?",
// six check boxes, the first four ticked; "Grouped tracks" ticked when a group header is selected)
class CloneTracksWindow final : public juce::AlertWindow
{
public:
    explicit CloneTracksWindow (bool groupHeaderSelected)
        : juce::AlertWindow (tr ("Clone"), tr ("What do you want to clone along with the track?"), juce::MessageBoxIconType::QuestionIcon)
    {
        const std::pair<juce::ToggleButton*, const char*> boxes[] = {
            { &patterns, TR_NOOP ("Patterns") }, { &audio, TR_NOOP ("Audio clips") }, { &automations, TR_NOOP ("Automations") },
            { &effects, TR_NOOP ("Effects") },   { &routing, TR_NOOP ("Mixer track routing") }, { &grouped, TR_NOOP ("Grouped tracks") }
        };
        for (const auto& [box, text] : boxes)
        {
            box->setButtonText (tr (text));
            box->setSize (300, 24);
            addCustomComponent (box);
        }
        for (auto* b : { &patterns, &audio, &automations, &effects })
            b->setToggleState (true, juce::dontSendNotification);
        grouped.setToggleState (groupHeaderSelected, juce::dontSendNotification);
        addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    }

    pltracks::CloneOptions options() const
    {
        pltracks::CloneOptions o;
        o.patterns = patterns.getToggleState();
        o.audioClips = audio.getToggleState();
        o.automations = automations.getToggleState();
        o.effects = effects.getToggleState();
        o.routing = routing.getToggleState();
        o.groupedTracks = grouped.getToggleState();
        return o;
    }

private:
    juce::ToggleButton patterns, audio, automations, effects, routing, grouped;
};
} // namespace

std::unique_ptr<juce::AlertWindow> PlaylistComponent::makeCloneTracksDialog (bool groupHeaderSelected)
{
    return std::make_unique<CloneTracksWindow> (groupHeaderSelected);
}

void PlaylistComponent::cloneTracksWithDialog (std::vector<int> tracks)
{
    const auto& song = engine.getSong();
    tracks.erase (std::remove_if (tracks.begin(), tracks.end(), [] (int t) { return t < 0; }), tracks.end());
    if (tracks.empty())
        return;
    // FL: at most 250 at once; all empty → "Nothing to clone"
    if (tracks.size() > 250)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Error"),
                                                tr ("It isn't possible to clone more than {0} playlist tracks at once. Please select fewer tracks and try again.",
                                                    { juce::String (250) }));
        return;
    }
    const auto header = std::any_of (tracks.begin(), tracks.end(), [&song] (int t) { return noteedit::isGroupHeader (song, t); });
    {
        auto probe = song;
        if (pltracks::cloneTracks (probe, tracks, {}) < 0)
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, tr ("Nothing to clone"),
                                                    tr ("All selected playlist tracks are empty. Since empty tracks can't be cloned, there is nothing to do."));
            return;
        }
    }
    auto* window = new CloneTracksWindow (header);
    juce::Component::SafePointer<PlaylistComponent> safe (this);
    window->enterModalState (true, juce::ModalCallbackFunction::create ([safe, window, tracks] (int result) {
                                 if (result != 1 || safe == nullptr)
                                     return;
                                 const auto options = window->options();
                                 auto run = [safe, tracks, options] {
                                     if (safe == nullptr)
                                         return;
                                     safe->setSelection ({});
                                     safe->engine.editSong ([tracks, options] (Song& s) {
                                         pltracks::cloneTracks (s, pltracks::tracksToClone (s, tracks, options.groupedTracks), options);
                                     });
                                     safe->sortClips();
                                     safe->refreshTrackLayout();
                                     safe->repaint();
                                 };
                                 // FL asks before cloning more than four tracks
                                 if (tracks.size() > 4)
                                     juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, tr ("This may take a while"),
                                                                         tr ("Are you sure you want to clone {0} playlist tracks at once? Cloning multiple tracks may take a long time.",
                                                                             { juce::String ((int) tracks.size()) }),
                                                                         tr ("Yes"), tr ("No"), nullptr,
                                                                         juce::ModalCallbackFunction::create ([run] (int r) { if (r != 0) run(); }));
                                 else
                                     run();
                             }),
                             true);
}

void PlaylistComponent::mergePatternClipsByKey()
{
    const auto& song = engine.getSong();
    const auto clips = pltracks::mergeCandidates (song, selection);
    // FUN_01302d30: a selection of nothing but automation clips goes to FL's automation merge
    if (! selection.empty() && std::all_of (selection.begin(), selection.end(), [&song] (int i) {
            return i >= 0 && i < (int) song.playlist.size() && song.playlist[(std::size_t) i].kind == ClipKind::automation;
        }))
    {
        mergeAutomationClipsByKey();
        return;
    }
    // FL merges only pattern clips; a selection of other kinds is refused, an unselected playlist uses its pattern clips
    std::vector<int> patterns;
    for (const auto i : clips)
        if (i >= 0 && i < (int) song.playlist.size() && song.playlist[(std::size_t) i].kind == ClipKind::pattern)
            patterns.push_back (i);
    if (! selection.empty() && patterns.size() != clips.size())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Can't merge"),
                                                tr ("The selection contains different clip types, can't merge."));
        return;
    }
    if (patterns.size() < 2)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Error"),
                                                tr ("Could not find enough valid pattern clips or instances to merge."));
        return;
    }
    int added = -1;
    setSelection ({});
    engine.editSong ([patterns, &added] (Song& s) { pltracks::mergePatternClips (s, patterns, &added); });
    sortClips();
}

void PlaylistComponent::mergeSimilarClipsByKey()
{
    const auto clips = pltracks::mergeCandidates (engine.getSong(), selection);
    {
        // the same refusal as FUN_012dc060's: fewer than two unmuted pattern clips
        const auto& song = engine.getSong();
        const auto usable = std::count_if (clips.begin(), clips.end(), [&song] (int i) {
            return i >= 0 && i < (int) song.playlist.size() && song.playlist[(std::size_t) i].kind == ClipKind::pattern
                   && ! song.playlist[(std::size_t) i].muted;
        });
        if (usable < 2)
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Error"),
                                                    tr ("Could not find enough valid pattern clips or instances to merge."));
            return;
        }
    }
    setSelection ({});
    engine.editSong ([clips] (Song& s) { pltracks::mergeSimilarPatternClips (s, clips); });
    sortClips();
}

void PlaylistComponent::mergeAutomationClipsByKey()
{
    const auto chosen = selection;
    const auto check = pltracks::checkAutomationMerge (engine.getSong(), chosen);
    if (check == pltracks::AutomationMergeCheck::tooFew || check == pltracks::AutomationMergeCheck::notAutomation)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Can't merge"),
                                                check == pltracks::AutomationMergeCheck::tooFew ? tr ("Please select at least two automation clips.")
                                                                                                : tr ("The selection may only contain automation clips."));
        return;
    }

    juce::Component::SafePointer<PlaylistComponent> safe (this);
    const auto lossy = check != pltracks::AutomationMergeCheck::ok;
    // the clips as they are now: the tool window isn't modal, so a later edit cancels the merge
    std::vector<Clip> before;
    for (const auto i : chosen)
        before.push_back (engine.getSong().playlist[(std::size_t) i]);
    const auto unchanged = [chosen, before] (const Song& song) {
        for (std::size_t k = 0; k < chosen.size(); ++k)
            if (chosen[k] >= (int) song.playlist.size() || ! (song.playlist[(std::size_t) chosen[k]] == before[k]))
                return false;
        return true;
    };
    const auto merge = [safe, chosen, lossy, unchanged] (pltracks::AutomationLinks links) {
        if (safe == nullptr)
            return;
        if (! lossy)
        {
            safe->setSelection ({});
            safe->engine.editSong ([chosen, links] (Song& s) { pltracks::mergeAutomationClips (s, chosen, links); });
            safe->sortClips();
            return;
        }
        // FUN_012ff0d0: FL's events-to-automation tool comes after the questions; Cancel there leaves everything as it was
        auto samples = std::make_shared<PatternEvents> (pltracks::automationMergeSamples (safe->engine.getSong(), chosen));
        openConvertToAutomationPanel ((int) samples->events.size(), 99.8,
                                      [samples] (float sensitivity) { return (int) eventedit::toAutomationPoints (*samples, sensitivity).size(); },
                                      [safe, chosen, links, unchanged] (float sensitivity) {
                                          if (safe == nullptr || ! unchanged (safe->engine.getSong()))
                                              return;
                                          safe->setSelection ({});
                                          safe->engine.editSong ([chosen, links, sensitivity] (Song& s) { pltracks::mergeAutomationClipsLossy (s, chosen, links, sensitivity); });
                                          safe->sortClips();
                                      });
    };
    // FL asks about the links last, and only about a real conflict
    const auto askLinks = [safe, chosen, merge] {
        if (safe == nullptr)
            return;
        if (! pltracks::automationLinksConflict (safe->engine.getSong(), chosen))
        {
            merge (pltracks::AutomationLinks::keepFirst);
            return;
        }
        auto* window = new juce::AlertWindow (tr ("Target links conflict"),
                                              tr ("The clips in the selection aren't all linked to the same parameters, what do you want to do?"),
                                              juce::MessageBoxIconType::QuestionIcon);
        window->addButton (tr ("Keep links of the first selected clip"), 1);
        window->addButton (tr ("Merge all links"), 2);
        window->addButton (tr ("Delete all links"), 3, juce::KeyPress (juce::KeyPress::returnKey)); // FL's default button
        window->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        window->enterModalState (true, juce::ModalCallbackFunction::create ([merge] (int result) {
                                     if (result == 1) merge (pltracks::AutomationLinks::keepFirst);
                                     else if (result == 2) merge (pltracks::AutomationLinks::mergeAll);
                                     else if (result == 3) merge (pltracks::AutomationLinks::deleteAll);
                                 }),
                                 true);
    };
    if (! lossy)
    {
        askLinks();
        return;
    }
    juce::AlertWindow::showYesNoCancelBox (
        juce::MessageBoxIconType::QuestionIcon, tr ("Can't perform lossless merge"),
        check == pltracks::AutomationMergeCheck::overlapping
            ? tr ("The selection contains overlapping clips. Perform lossy merge?")
            : tr ("The selection contains at least one clip that doesn't have control points at both the start and end of its region. Perform lossy merge?"),
        tr ("Yes"), tr ("No"), {}, nullptr,
        juce::ModalCallbackFunction::create ([askLinks] (int result) { if (result == 1) askLinks(); }));
}

void PlaylistComponent::makeSelectionUnique()
{
    if (selection.empty())
        return;
    auto run = [this, chosen = selection] {
        engine.editSong ([chosen] (Song& s) {
            std::vector<int> patterns, audio;
            for (const auto i : chosen)
                if (i >= 0 && i < (int) s.playlist.size())
                {
                    const auto kind = s.playlist[(std::size_t) i].kind;
                    (kind == ClipKind::audio ? audio : patterns).push_back (i);
                }
            polish::makeClipsUnique (s, patterns);
            // each audio source of the selection gets its own channel copy
            std::vector<int> channels;
            for (const auto i : audio)
                if (const auto ch = s.playlist[(std::size_t) i].channel; std::find (channels.begin(), channels.end(), ch) == channels.end())
                    channels.push_back (ch);
            for (const auto ch : channels)
            {
                std::vector<int> group;
                for (const auto i : audio)
                    if (s.playlist[(std::size_t) i].channel == ch)
                        group.push_back (i);
                clipops::makeAudioUnique (s, group);
            }
        });
    };
    // FL (FUN_0123d640): asks from 20 selected clips on
    if (selection.size() >= 20)
    {
        juce::Component::SafePointer<PlaylistComponent> safe (this);
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, tr ("Make all selected clips unique?"),
                                            tr ("Are you sure you want to make all {0} selected clips unique?", { juce::String ((int) selection.size()) }),
                                            tr ("Yes"), tr ("No"), nullptr,
                                            juce::ModalCallbackFunction::create ([safe, run] (int r) { if (r != 0 && safe != nullptr) run(); }));
        return;
    }
    run();
}

void PlaylistComponent::makeSelectionUniqueAsSample()
{
    // FL's Edit › Make unique as sample (EEMakeUniqueAsSampleAction, Tag 1 of FUN_0123d640 -> FUN_0123dc00): what each
    // selected audio clip plays is saved as "<sample> - Part_<n>.wav" (an earlier " - Part_" suffix is dropped first), in
    // the project's Samples folder, without a file dialog from this menu; a new audio clip channel copied from the old one
    // plays the file, and the clip now starts at the file's start. The clip's own pitch, stretch and reverse are rendered
    // into the file and reset on the clip (round 189l), so it sounds the same. LYNX writes the file next to the source
    // sample when the project has no data folder.
    const auto& song = engine.getSong();
    struct Part
    {
        int clip;
        std::string path, name;
    };
    std::vector<Part> parts;
    for (const auto i : selection)
    {
        if (i < 0 || i >= (int) song.playlist.size())
            continue;
        const auto& c = song.playlist[(std::size_t) i];
        if (c.kind != ClipKind::audio || c.channel < 0 || c.channel >= (int) song.channels.size())
            continue;
        const juce::File source (juce::String::fromUTF8 (song.channels[(std::size_t) c.channel].samplePath.c_str()));
        const auto audio = engine.renderClipAudio ((std::size_t) i);
        if (audio.left.empty())
            continue;

        auto folder = song.dataFolder.empty() ? source.getParentDirectory() : juce::File (juce::String::fromUTF8 (song.dataFolder.c_str())).getChildFile ("Samples");
        if (! folder.createDirectory().wasOk())
            folder = source.getParentDirectory();
        auto base = source.getFileNameWithoutExtension();
        if (const auto at = base.lastIndexOf (" - Part_"); at > 0)
            base = base.substring (0, at);
        juce::File out;
        for (int n = 1;; ++n)
            if (out = folder.getChildFile (base + " - Part_" + juce::String (n) + ".wav"); ! out.exists())
                break;

        const auto frames = (int) audio.left.size();
        const auto stereo = audio.left != audio.right;
        juce::AudioBuffer<float> buffer (stereo ? 2 : 1, frames);
        buffer.copyFrom (0, 0, audio.left.data(), frames);
        if (stereo)
            buffer.copyFrom (1, 0, audio.right.data(), frames);
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream (out.createOutputStream());
        std::unique_ptr<juce::AudioFormatWriter> writer (stream != nullptr ? wav.createWriterFor (stream.get(), audio.sampleRate, (unsigned int) buffer.getNumChannels(), 32, {}, 0) : nullptr);
        if (writer == nullptr)
            continue;
        stream.release(); // (the writer owns it now)
        if (! writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples()))
            continue;
        writer.reset();
        parts.push_back ({ i, out.getFullPathName().toStdString(), out.getFileNameWithoutExtension().toStdString() });
    }
    if (parts.empty())
        return;

    engine.editSong ([parts] (Song& s) {
        for (const auto& p : parts)
        {
            auto& clip = s.playlist[(std::size_t) p.clip];
            auto copy = s.channels[(std::size_t) clip.channel];
            copy.samplePath = p.path;
            copy.name = quickrender::uniqueChannelName (s, p.name);
            s.channels.push_back (std::move (copy));
            clipops::resetRenderedClip (clip, (int) s.channels.size() - 1);
        }
    });
}

void PlaylistComponent::lockAllTracks (bool lock)
{
    // FL (FUN_011c2190): "Resize tracks?|Also resize all playlist tracks to their minimum / default size?" Yes / No / Cancel
    juce::Component::SafePointer<PlaylistComponent> safe (this);
    juce::AlertWindow::showYesNoCancelBox (juce::MessageBoxIconType::QuestionIcon, tr ("Resize tracks?"),
                                           lock ? tr ("Also resize all playlist tracks to their minimum size?")
                                                : tr ("Also resize all playlist tracks to their default size?"),
                                           tr ("Yes"), tr ("No"), tr ("Cancel"), nullptr,
                                           juce::ModalCallbackFunction::create ([safe, lock] (int result) {
                                               if (result == 0 || safe == nullptr) // 1 = yes, 2 = no, 0 = cancel
                                                   return;
                                               const auto count = safe->numTracks();
                                               safe->engine.editSong ([count, lock, resize = result == 1] (Song& s) { pltracks::lockAll (s, count, lock, resize); });
                                               safe->refreshTrackLayout();
                                               safe->updateScrollBars();
                                               safe->repaint();
                                           }));
}

void PlaylistComponent::resetEmptyTracksAsked()
{
    juce::Component::SafePointer<PlaylistComponent> safe (this);
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, tr ("Reset empty tracks?"),
                                        tr ("Reset all empty playlist tracks in the current arrangement?"), tr ("OK"), tr ("Cancel"), nullptr,
                                        juce::ModalCallbackFunction::create ([safe] (int result) {
                                            if (result == 0 || safe == nullptr)
                                                return;
                                            safe->engine.editSong ([] (Song& s) { pltracks::resetEmptyTracks (s); });
                                            safe->refreshTrackLayout();
                                            safe->repaint();
                                        }));
}

void PlaylistComponent::setStretchOnResize (bool on)
{
    EditorGlobals::get().stretchOnResize = on;
    stretchButton.setToggleState (on, juce::dontSendNotification);
    ui::postHint ({ tr ("Stretch audio when resizing audio clips"), on ? tr ("On") : tr ("Off"), false, false });
    repaint();
}

std::int64_t PlaylistComponent::songPositionTick() const
{
    return (std::int64_t) engine.songPlayheadTicks();
}

void PlaylistComponent::addMarkerByKey()
{
    // FL (FUN_01229d20): the song position when it is in view, else the bar line nearest the view's left edge; the
    // time selection's start wins over both. Then the name dialog.
    const auto& song = engine.getSong();
    const auto left = (std::int64_t) std::max (0.0, scrollTicks);
    const auto right = (std::int64_t) (scrollTicks + gridArea().getWidth() / pixelsPerTick);
    auto tick = songPositionTick();
    if (tick < left || tick > right)
    {
        const auto bar = song.barAt (left);
        tick = left - bar.tick > bar.length / 2 ? bar.tick + bar.length : bar.tick;
    }
    if (song.hasLoop())
        tick = song.loopStart;
    editMarkerName (tick, -1);
}

void PlaylistComponent::addAutoMarker()
{
    // FL (FUN_00e546a0 with FUN_00e43fa0 (snap)): the song position on the grid
    const auto tick = snap ((double) songPositionTick(), true);
    for (const auto& m : engine.getSong().markers())
        if (m.tick == tick)
            return; // FL keeps the marker that is there (and no undo step)
    engine.editSong ([tick] (Song& s) { markers::addAuto (s, tick); });
}

void PlaylistComponent::addSignatureChangeByKey()
{
    // FL (FUN_0122bd20): the time selection (start, and its end gets the old signature back), else the song position
    const auto& song = engine.getSong();
    const auto from = song.hasLoop() ? song.loopStart : snap ((double) songPositionTick(), true);
    const auto to = song.hasLoop() ? song.loopEnd : (std::int64_t) -1;
    const auto current = markers::signatureAt (song, from);

    auto* window = new juce::AlertWindow (tr ("Add time signature change..."), {}, juce::MessageBoxIconType::NoIcon, this);
    juce::StringArray numerators, denominators;
    for (int n = 1; n <= 32; ++n)
        numerators.add (juce::String (n));
    for (int d : { 1, 2, 4, 8, 16, 32 })
        denominators.add (juce::String (d));
    window->addComboBox ("numerator", numerators, tr ("Numerator"));
    window->addComboBox ("denominator", denominators, tr ("Denominator"));
    window->getComboBoxComponent ("numerator")->setText (juce::String (current.first), juce::dontSendNotification);
    window->getComboBoxComponent ("denominator")->setText (juce::String (current.second), juce::dontSendNotification);
    window->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    window->enterModalState (true, juce::ModalCallbackFunction::create ([this, from, to, window] (int result) {
                                 if (result != 1)
                                     return;
                                 const auto n = window->getComboBoxComponent ("numerator")->getText().getIntValue();
                                 const auto d = window->getComboBoxComponent ("denominator")->getText().getIntValue();
                                 engine.editSong ([from, to, n, d] (Song& s) { markers::addSignatureChange (s, from, to, n, d); });
                             }),
                             true);
}

void PlaylistComponent::selectTimeAroundSelection()
{
    // FL (FUN_00eb1b90 → FUN_00e4ea50): the time selection spans the selected clips, or the view when none is
    // selected; the same span again clears it
    const auto& song = engine.getSong();
    std::int64_t from = -1, to = -1;
    for (auto i : selection)
        if (i >= 0 && i < (int) song.playlist.size())
        {
            const auto& c = song.playlist[(std::size_t) i];
            from = from < 0 ? c.start : std::min (from, c.start);
            to = std::max (to, c.end());
        }
    if (from < 0)
    {
        from = (std::int64_t) std::llround (std::max (0.0, scrollTicks));
        to = (std::int64_t) std::llround (std::max (0.0, scrollTicks) + gridArea().getWidth() / pixelsPerTick);
    }
    const auto same = song.hasLoop() && song.loopStart == from && song.loopEnd == to;
    engine.editSong ([from, to, same] (Song& s) {
        s.loopStart = same ? 0 : from;
        s.loopEnd = same ? 0 : to;
    }, false);
    repaint();
}

void PlaylistComponent::moveTimeSelection (int direction)
{
    // FL (FUN_00e43ea0, the action's Tag ±1): the time selection moves by its own length (not before the song's
    // start) and the song position goes to its start
    const auto& song = engine.getSong();
    if (! song.hasLoop())
        return;
    const auto length = song.loopEnd - song.loopStart;
    const auto from = song.loopStart + direction * length;
    const auto songMode = song.mode == PlayMode::song;
    if (from < 0)
        return;
    engine.editSong ([from, length] (Song& s) {
        s.loopStart = from;
        s.loopEnd = from + length;
    }, false);
    if (! songMode)
        engine.editSong ([] (Song& s) { s.mode = PlayMode::song; }, false);
    engine.jumpTo ((double) from);
    playheadTick = (double) from;
    // keep it in view
    const auto visible = gridArea().getWidth() / pixelsPerTick;
    if ((double) from < scrollTicks || (double) (from + length) > scrollTicks + visible)
    {
        scrollTicks = std::max (0.0, (double) from - visible * 0.1);
        updateScrollBars();
    }
    repaint();
}

void PlaylistComponent::placeLoopMarker()
{
    // FL (FUN_0122bb80): the bar line nearest the view's left edge, or the time selection's start
    const auto& song = engine.getSong();
    const auto left = (std::int64_t) std::max (0.0, scrollTicks);
    const auto bar = song.barAt (left);
    auto tick = left - bar.tick > bar.length / 2 ? bar.tick + bar.length : bar.tick;
    if (song.hasLoop())
        tick = song.loopStart;
    engine.editSong ([tick] (Song& s) { markers::placeSongLoop (s, tick); });
}

void PlaylistComponent::pitchSelectedAudioClips (int semitones)
{
    // FL (FUN_00e2d7b0): only the selected clips that are audio clips; nothing selected, nothing changes
    const auto targets = selection;
    engine.editSong ([targets, semitones] (Song& s) { clipops::pitchAudioClips (s, targets, semitones); });
    repaint();
}

void PlaylistComponent::reverseSelectedAudioClips()
{
    const auto targets = selection;
    // each clip's file length in ticks at its own speed, read here (the sample library is not for the edit lambda)
    std::vector<std::int64_t> ticks;
    {
        const auto& song = engine.getSong();
        for (auto i : targets)
        {
            std::int64_t t = 0;
            if (i >= 0 && i < (int) song.playlist.size())
                if (const auto& c = song.playlist[(std::size_t) i]; c.kind == ClipKind::audio && c.channel >= 0 && c.channel < (int) song.channels.size())
                {
                    const auto& channel = song.channels[(std::size_t) c.channel];
                    if (const auto sample = engine.getSampleLibrary().load (juce::String::fromUTF8 (channel.samplePath.c_str())); sample != nullptr)
                        t = clipops::fileTicks (channel, c, (double) clipops::clipSpan (channel, sample).frames, sample->sampleRate, song.bpm);
                }
            ticks.push_back (t);
        }
    }
    engine.editSong ([targets, ticks] (Song& s) {
        clipops::reverseAudioClips (s, targets, [&] (int clip) {
            for (std::size_t k = 0; k < targets.size(); ++k)
                if (targets[k] == clip)
                    return ticks[k];
            return (std::int64_t) 0;
        });
    });
    repaint();
}

void PlaylistComponent::quantizeClips (double strength)
{
    std::vector<int> targets = selection;

    if (targets.empty())
        for (int i = 0; i < (int) engine.getSong().playlist.size(); ++i)
            targets.push_back (i);

    if (targets.empty())
        return;

    const auto grid = gridTicks();
    engine.editSong ([&targets, grid, strength] (Song& s) { transportedit::quantizeClipStarts (s.playlist, targets, grid, strength); });
    sortClips();
}

void PlaylistComponent::deleteSelection()
{
    if (selection.empty())
        return;

    auto doomed = selection;
    setSelection ({});
    engine.editSong ([&doomed] (Song& s) {
        std::sort (doomed.rbegin(), doomed.rend());

        for (auto i : doomed)
            if (i >= 0 && i < (int) s.playlist.size())
                s.playlist.erase (s.playlist.begin() + i);
    });
}

void PlaylistComponent::copySelection()
{
    clipboard.clear();
    ++clipboardStamp;

    for (auto i : selection)
        clipboard.push_back (engine.getSong().playlist[(std::size_t) i]);
}

void PlaylistComponent::pasteClips (std::int64_t at)
{
    if (clipboard.empty())
        return;

    // pastes at `at` (the clip menu's click), else at the playback cursor (or where the first clip was),
    // keeping tracks and spacing
    std::int64_t first = clipboard.front().start;
    for (const auto& c : clipboard)
        first = juce::jmin (first, c.start);

    const auto target = at >= 0 ? at : playheadTick >= 0.0 ? snap (playheadTick, true) : first;
    std::vector<int> added;

    engine.editSong ([&added, target, first] (Song& s) {
        for (auto c : clipboard)
        {
            if (c.kind == ClipKind::pattern && c.pattern >= (int) s.patterns.size())
                continue;
            if (c.kind == ClipKind::audio && c.channel >= (int) s.channels.size())
                continue;
            if (c.kind == ClipKind::automation && c.automation >= (int) s.automations.size())
                continue;

            c.start = c.start - first + target;
            added.push_back ((int) s.playlist.size());
            s.playlist.push_back (c);
        }
    });
    setSelection (added);
    sortClips (added);
}

void PlaylistComponent::duplicateSelection()
{
    const auto& song = engine.getSong();

    // with a time selection: repeats it right after itself (the selected clips that start inside it, or
    // all the clips that do), and the time selection moves on so the next Ctrl+B continues
    if (song.hasLoop())
    {
        const auto from = song.loopStart, to = song.loopEnd;
        std::vector<int> sources;

        for (auto i : selection)
            if (song.playlist[(std::size_t) i].start >= from && song.playlist[(std::size_t) i].start < to)
                sources.push_back (i);

        std::vector<int> added;
        engine.editSong ([&] (Song& s) {
            added = noteedit::repeatTime (s.playlist, from, to, sources);
            s.loopStart = to;
            s.loopEnd = to + (to - from);
        });
        setSelection (added);
        sortClips (added);
        return;
    }

    if (selection.empty())
        return;

    const auto bar = (std::int64_t) ppq * juce::jmax (1, song.beatsPerBar);
    std::int64_t from = INT64_MAX, to = 0;

    for (auto i : selection)
    {
        from = juce::jmin (from, song.playlist[(std::size_t) i].start);
        to = juce::jmax (to, song.playlist[(std::size_t) i].end());
    }

    const auto offset = juce::jmax (bar, (to - from + bar - 1) / bar * bar); // like FL's Ctrl+B
    const auto source = selection;
    std::vector<int> added;

    engine.editSong ([&added, &source, offset] (Song& s) {
        for (auto i : source)
        {
            auto c = s.playlist[(std::size_t) i];
            c.start += offset;
            added.push_back ((int) s.playlist.size());
            s.playlist.push_back (c);
        }
    });
    setSelection (added);
    sortClips (added);
}

bool PlaylistComponent::handleKey (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    const auto cmd = mods.isCommandDown();
    const auto code = key.getKeyCode();
    const auto letter = keys::letterOfKeyCode (code); // (0 for F-keys, numpad, arrows: issue #12)

    if (code == juce::KeyPress::backspaceKey)
    {
        EditorGlobals::get().toggleGrid();
        return true;
    }

    // Ctrl+Ins / Ctrl+Del: insert / remove the time selection's length across all tracks
    if (cmd && ! mods.isShiftDown() && ! mods.isAltDown() && code == juce::KeyPress::insertKey) // (Ctrl+Shift+Ins: pattern.insert; Ctrl+Alt+Ins: playlist.sliceInsertSpace)
    {
        insertTimeSelection();
        return true;
    }

    if (cmd && ! mods.isShiftDown() && code == juce::KeyPress::deleteKey) // (Ctrl+Shift+Del: pattern.delete)
    {
        deleteTimeSelection();
        return true;
    }

    if (code == juce::KeyPress::deleteKey && ! mods.isShiftDown())
    {
        if (ripple)
            rippleDeleteSelection();
        else
            deleteSelection();
        return true;
    }

    if (code == juce::KeyPress::escapeKey)
    {
        setSelection ({});
        return true;
    }

    if (cmd && letter == 'A')
    {
        selectAllWithTime();
        return true;
    }

    if (cmd && ! mods.isShiftDown() && ! mods.isAltDown() && letter == 'C') // (Ctrl+Shift+C: pattern.clone; Ctrl+Alt+C: consolidate)
    {
        copySelection();
        return true;
    }

    if (cmd && letter == 'X')
    {
        copySelection();
        deleteSelection();
        return true;
    }

    if (cmd && ! mods.isShiftDown() && letter == 'V') // (Ctrl+Shift+V: playlist.makeUnique)
    {
        pasteClips();
        return true;
    }

    if (cmd && ! mods.isShiftDown() && letter == 'B') // (Ctrl+Shift+B: playlist.cloneTracks)
    {
        duplicateSelection();
        return true;
    }

    // (the tool letters are key map actions now: runKeyAction, issue #12)
    return false;
}

bool PlaylistComponent::runKeyAction (const std::string& id)
{
    for (const auto& t : editTools())
        if (t.tool != EditTool::drumPaint && id == "playlist.tool." + std::string (editToolId (t.tool)))
        {
            tools.setTool (t.tool);
            return true;
        }
    return false;
}

void PlaylistComponent::refreshToolKeys (const std::function<juce::String (const std::string&)>& keyOf)
{
    tools.refreshKeys ([&keyOf] (EditTool t) { return keyOf ("playlist.tool." + std::string (editToolId (t))); });
}

void PlaylistComponent::mouseMove (const juce::MouseEvent& e)
{
    updateHover (e.getPosition(), e.mods);
    feedMagicLasso (e.getPosition(), false);
}

void PlaylistComponent::feedMagicLasso (juce::Point<int> p, bool buttonDown)
{
    if (! EditorGlobals::get().magicLasso)
        return;
    const auto grid = gridArea();
    // (FL clamps the point into the clip area, FUN_00e6f840)
    const auto at = grid.getConstrainedPoint (p);
    const auto loop = lassoPath.move ({ (double) at.x, (double) at.y }, juce::Time::getMillisecondCounter(), buttonDown || ! grid.contains (p));
    if (loop.empty())
        return;

    const auto& song = engine.getSong();
    std::vector<int> enclosed;
    std::vector<bool> enclosedSelected;
    for (int i = 0; i < (int) song.playlist.size(); ++i)
    {
        const auto r = clipBounds (i);
        if (! r.intersects (grid) || song.playlist[(std::size_t) i].gridSlot >= 0)
            continue;
        if (magiclasso::rectInsidePolygon (loop, magiclasso::innerRect ({ (double) r.getX(), (double) r.getY(), (double) r.getRight(), (double) r.getBottom() })))
        {
            enclosed.push_back (i);
            enclosedSelected.push_back (std::find (selection.begin(), selection.end(), i) != selection.end());
        }
    }
    const auto shift = juce::ModifierKeys::currentModifiers.isShiftDown();
    switch (magiclasso::decide (enclosed, shift, (int) selection.size(), enclosedSelected))
    {
        case magiclasso::Result::select:
        {
            auto chosen = selection;
            for (auto i : enclosed)
                if (std::find (chosen.begin(), chosen.end(), i) == chosen.end())
                    chosen.push_back (i);
            setSelection (chosen);
            lassoPath.clear();
            break;
        }
        case magiclasso::Result::zoomToSelection:
        {
            std::int64_t from = std::numeric_limits<std::int64_t>::max(), to = 0;
            for (auto i : selection)
            {
                from = std::min (from, song.playlist[(std::size_t) i].start);
                to = std::max (to, song.playlist[(std::size_t) i].end());
            }
            if (to > from)
                zoomToTicks ((double) from, (double) to);
            // (FL 2026 also fits the used tracks into the view's height: shot g35)
            auto last = -1;
            for (const auto& c : song.playlist)
                last = std::max (last, c.track);
            if (last >= 0 && last + 1 < (int) trackTops.size() && trackTops[(std::size_t) last + 1] > 0)
            {
                setTrackZoom (trackZoom * (double) gridArea().getHeight() / (double) trackTops[(std::size_t) last + 1], tracksTop());
                scrollY = 0;
                updateScrollBars();
                repaint();
            }
            lassoPath.clear();
            break;
        }
        case magiclasso::Result::nothing:
            break;
    }
}

void PlaylistComponent::mouseExit (const juce::MouseEvent&)
{
    lastMouse = { -1, -1 };

    if (! (hover == Hover {}))
    {
        hover = {};
        repaint();
    }
}

void PlaylistComponent::modifierKeysChanged (const juce::ModifierKeys& mods)
{
    if (drag == Drag::none && lastMouse.x >= 0)
        updateCursor (mods);
}

juce::Rectangle<int> PlaylistComponent::clipBounds (int index) const
{
    const auto& c = engine.getSong().playlist[(std::size_t) index];
    const auto x0 = (float) tickToX ((double) c.start), x1 = (float) tickToX ((double) c.end());
    return juce::Rectangle<float> (x0, (float) trackToY (c.track) + 1.0f, juce::jmax (3.0f, x1 - x0 - 1.0f), (float) heightOf (c.track) - 3.0f)
        .getSmallestIntegerContainer();
}

juce::Rectangle<int> PlaylistComponent::clipMenuButton (int index) const
{
    const auto r = clipBounds (index);

    if (r.getWidth() < 40)
        return {};

    return { r.getX() + 2, r.getY() + 1, 13, (int) clipTitleHeight - 2 };
}

PlaylistComponent::Hover PlaylistComponent::hoverAt (juce::Point<int> p) const
{
    Hover h;

    if (p.x < headerWidth && p.y >= tracksTop() && p.y < gridArea().getBottom())
    {
        h.header = true;
        h.track = yToTrack (p.y);
        h.divider = dividerAt (p);
        const auto folded = noteedit::isFoldedAway (engine.getSong(), h.track);
        h.led = ! folded && h.divider < 0 && trackLed (h.track).expanded (3).contains (p);
        h.arrow = ! folded && h.divider < 0 && trackArrow (h.track).contains (p);
        h.arm = ! folded && h.divider < 0 && trackArm (h.track).expanded (2).contains (p);
        h.input = ! folded && h.divider < 0 && trackInput (h.track).expanded (2).contains (p);
        if (simpleHeaders && ! folded && h.divider < 0)
        {
            h.solo = trackSolo (h.track).contains (p);
            h.volume = trackVolume (h.track).expanded (0, 4).contains (p);
            h.pan = trackPan (h.track).expanded (2).contains (p);
            h.fx = trackFx (h.track).contains (p);
        }
        h.audition = ! folded && h.divider < 0 && trackAudition (h.track).expanded (2).contains (p);
        return h;
    }

    if (! gridArea().contains (p))
        return h;

    h.track = yToTrack (p.y);
    bool rightEdge = false, leftEdge = false;
    h.clip = clipAt (p.x, p.y, rightEdge, leftEdge);

    if (h.clip < 0)
        return h;

    const auto& c = engine.getSong().playlist[(std::size_t) h.clip];
    h.target = rightEdge ? gesture::Target::rightEdge : leftEdge ? gesture::Target::leftEdge : gesture::Target::item;

    if (clipMenuButton (h.clip).contains (p))
        h.target = gesture::Target::clipMenu;
    else if (fadeHandleAt (h.clip, p) != 0)
        h.target = gesture::Target::fadeHandle;
    else if (gainHandle (h.clip).expanded (2.0f).contains (p.toFloat()))
        h.target = gesture::Target::gainHandle;
    else if (c.kind == ClipKind::automation && automationPointAt (h.clip, p) >= 0)
        h.target = gesture::Target::autoPoint;
    else if (c.kind == ClipKind::automation && automationHandleAt (h.clip, p) >= 0)
        h.target = gesture::Target::autoTension;
    else if (c.kind == ClipKind::automation && isOnAutomationCurve (h.clip, p))
        h.target = gesture::Target::autoCurve;

    return h;
}

void PlaylistComponent::updateHover (juce::Point<int> p, juce::ModifierKeys mods)
{
    lastMouse = p;
    const auto h = hoverAt (p);

    if (! (h == hover))
    {
        // repaint the old and new track rows (header and lane)
        for (auto t : { hover.track, h.track })
            if (t >= 0)
                repaint (0, trackToY (t) - 4, getWidth(), heightOf (t) + 8);
        hover = h;
        // the two comping gestures say what they do (round 177c)
        if (hover.audition)
            ui::postHint ({ tr ("Audition this take: it plays instead of the comp"), {}, false, false });
        else if (! hover.header && hover.track >= 0 && hover.target == gesture::Target::item)
            if (const auto group = comp::groupOf (engine.getSong(), hover.track); group.isLane (hover.track) && comp::isComp (engine.getSong(), group.comp))
                ui::postHint ({ tr ("Drag across this take to send that range to the comp. Drag its title to move it"), {}, false, false });
    }

    // round 177b: the lanes' arrow and "+A" light under the mouse
    if (const auto onLaneButton = h.track >= 0 && p.x < headerWidth && (trackAddLane (h.track).contains (p) || trackLaneArrow (h.track).contains (p));
        onLaneButton != laneButtonHover)
    {
        laneButtonHover = onLaneButton;
        repaint (0, trackToY (h.track), headerWidth, heightOf (h.track));
    }

    if (const auto onZoom = trackZoomHandle().contains (p); onZoom != trackZoomHover)
    {
        trackZoomHover = onZoom;
        repaint (trackZoomHandle());
    }

    if (drag == Drag::none)
        updateCursor (mods);
}

void PlaylistComponent::updateCursor (juce::ModifierKeys mods)
{
    const auto p = lastMouse;
    auto cursor = gesture::Cursor::normal;

    if ((hover.header && hover.divider >= 0) || trackZoomHandle().contains (p))
    {
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        return;
    }
    if (const auto lane = laneAt (p); lane.track >= 0) // round 177b
    {
        if (p.x >= headerWidth && drawmode::isOn()) // round 190ef: Draw Mode's pencil
            setMouseCursor (editorCursor (gesture::Cursor::draw));
        else
            setMouseCursor (p.x >= headerWidth && lanePointAt (lane.track, lane.lane, p) >= 0 ? juce::MouseCursor::DraggingHandCursor
                                                                                               : juce::MouseCursor::NormalCursor);
        return;
    }
    if (hover.header)
        cursor = hover.led || hover.arrow || hover.arm || hover.input || hover.audition || hover.solo || hover.volume || hover.pan
                         || hover.fx ? gesture::Cursor::point : gesture::Cursor::normal;
    else if (gridArea().contains (p))
    {
        const gesture::Mods m { mods.isShiftDown(), flmods::ctrl (mods), mods.isAltDown() };
        const auto tool = tools.getTool();
        cursor = gesture::cursorFor (gesture::leftAction (tool, hover.target, m, true), tool);
    }
    else if (p.y >= rulerTop && p.y < headerHeight && p.x >= headerWidth)
        cursor = flmods::ctrl (mods) ? gesture::Cursor::select : gesture::Cursor::point;

    setMouseCursor (editorCursor (cursor));
}

ui::Hint PlaylistComponent::hintAt (juce::Point<int> p)
{
    ui::Hint hint;
    const auto mods = juce::ModifierKeys::currentModifiers;
    const auto& song = engine.getSong();
    const auto h = hoverAt (p);
    auto trackName = [&song] (int t) {
        return t < (int) song.playlistTracks.size() && ! song.playlistTracks[(std::size_t) t].name.empty()
                   ? juce::String::fromUTF8 (song.playlistTracks[(std::size_t) t].name.c_str())
                   : tr ("Track {0}", { juce::String (t + 1) });
    };

    if (const auto lane = laneAt (p); lane.track >= 0) // round 177b
    {
        const auto a = laneAutomation (lane.track, lane.lane);
        hint.text = p.x < headerWidth ? tr ("Automation lane: [RMB] lane menu  [2xLMB] edit the points")
                    : drawmode::isOn() ? tr ("Automation lane, Draw Mode: [LMB] drag draws steps at the grid width  [Alt] freehand  [RMB] deletes a point")
                                       : tr ("Automation lane: [LMB] add or drag a point  [RMB] deletes a point  [Alt] drag bends the line  [Shift] adds on the line");
        hint.hasMenu = true;
        if (a >= 0)
        {
            const auto& automation = song.automations[(std::size_t) a];
            hint.value = juce::String::fromUTF8 (automation.name.c_str());
            if (p.x >= headerWidth)
                hint.value << "  " << automationPointValueText (automation.target, automation.valueAt (juce::jmax (0.0, xToTick (p.x))));
        }
        return hint;
    }
    if (p.x < headerWidth && p.y >= headerHeight)
    {
        const auto track = yToTrack (p.y);
        if (trackLaneArrow (track).expanded (2).contains (p))
            return { tr ("Show / hide this track's automation lanes"), {} };
        if (trackAddLane (track).contains (p))
            return { tr ("Add an automation lane under this track"), {} };
    }

    if (trackZoomHandle().contains (p))
    {
        hint.text = tr ("Track height (vertical zoom): [LMB] drag up / down  [Alt][Wheel] over the Playlist  [2xLMB] 100%");
        hint.value = juce::String (juce::roundToInt (trackZoom * 100.0)) + "%";
        return hint;
    }

    if (h.header && h.divider >= 0)
    {
        hint.text = tr ("Track divider: [LMB] drag resizes this track  [Shift][LMB] drag: all tracks  [2xLMB] default height");
        hint.value = juce::String (heightOf (h.divider)) + " px";
        return hint;
    }

    if (h.header && simpleInsert (h.track) > 0 && (h.led || h.solo || h.volume || h.pan || h.fx))
    {
        // the Simple headers (round 184a): their controls are the track's mixer track's
        const auto& m = song.mixer[(std::size_t) simpleInsert (h.track)];
        hint.text = h.led      ? tr ("Track: [LMB] mute / unmute its mixer track")
                    : h.solo   ? tr ("Track: [LMB] solo its mixer track")
                    : h.volume ? tr ("Track volume: [LMB] click or drag  [Shift] fine  [2xLMB] 0 dB")
                    : h.pan    ? tr ("Track pan: [LMB] drag up / down  [Shift] fine  [2xLMB] centre")
                               : tr ("Track effects: [LMB] open the effects of its mixer track");
        if (h.volume)
            hint.value = juce::String (juce::Decibels::gainToDecibels (m.volume / unityFader), 1) + " dB";
        else if (h.pan)
            hint.value = juce::String (juce::roundToInt (m.pan * 100.0f));
        else if (h.fx && ! m.chainPreset.empty()) // round 190dg: the track's chain preset, as the dock names it
            hint.value = juce::String::fromUTF8 (m.chainPreset.c_str())
                         + (chains::unchanged (song, simpleInsert (h.track)) ? juce::String() : " " + tr ("(edited)"));
        else
            hint.value = trackName (h.track);
        return hint;
    }

    if (h.header)
    {
        hint.text = h.arm     ? tr ("Audio track: [LMB] arm / disarm recording (its mixer track)")
                    : h.input ? tr ("Audio track: [LMB] audio input source and monitoring (its mixer track)")
                    : h.led   ? tr ("Track: [LMB] mute / unmute  [Ctrl][LMB] or [RMB] solo")
                    : h.arrow ? tr ("Track group: [LMB] collapse / expand the grouped tracks")
                              : tr ("Track: [LMB] select  [Ctrl][LMB] add / remove  [Shift][LMB] range  [RMB] menu  [2xLMB] open the clip source  [Shift][Wheel] move up / down");
        hint.value = trackName (h.track);
        if (const auto mode = trackmodes::describe (song, h.track); ! mode.empty()) // FL's "(Instrument track)" / "(Audio track, ...)"
            hint.value += " " + (mode.rfind ("(Audio", 0) == 0 ? tr ("(Audio track, linked to mixer track {0})", { juce::String (song.playlistTracks[(std::size_t) h.track].mixerTrack) })
                                                                 : tr ("(Instrument track)"));
        hint.hasMenu = ! h.led && ! h.arrow && ! h.arm && ! h.input;
        return hint;
    }

    if (p.y >= rulerTop && p.y < headerHeight && p.x >= headerWidth)
    {
        // over a marker's tag: its action, and that a click jumps there (FL)
        if (const auto m = markerTagAt (p.x, p.y); m >= 0)
        {
            const auto& marker = song.markers()[(std::size_t) m];
            hint.text = tr ("Marker: [LMB] jump to it (while playing: at the next Quantize song jumps step)  [Ctrl][LMB] select up to the next marker  [RMB] action, rename, delete");
            hint.value = markerLabel (marker);
            if (const auto* action = markers::actionName (marker.isSignature() ? Marker::timeSignature : marker.action); *action != 0)
                hint.value += " (" + tr (action) + ")";
            hint.hasMenu = true;
            return hint;
        }
        hint.text = tr ("Ruler: [LMB] song position  [RMB] or [Ctrl][LMB] or [2xLMB] drag: time selection (loop)  [Shift][LMB] drag: move it  [Ctrl][RMB] drag or [Ctrl][Wheel] or a pinch: zoom  [RMB] click: menu  [Alt] no snap");
        hint.hasMenu = true;
        return hint;
    }

    if (! gridArea().contains (p))
        return hint;

    const gesture::Mods m { mods.isShiftDown(), flmods::ctrl (mods), mods.isAltDown() };
    hint.text = actionHint (gesture::leftAction (tools.getTool(), h.target, m, true), true);

    if (h.clip >= 0)
    {
        const auto onHeader = p.y < clipBounds (h.clip).getY() + (int) clipTitleHeight;
        if (h.target == gesture::Target::gainHandle)
            hint.text = actionHint (gesture::Action::clipGain, true);
        else if (h.target == gesture::Target::clipMenu)
            hint.text = song.playlist[(std::size_t) h.clip].kind == ClipKind::audio
                            ? tr ("Clip menu: [LMB] or [RMB] opens it (pitch, fit to tempo, reverse, chop, make unique...)")
                            : tr ("Clip menu: [LMB] or [RMB] opens it (make unique, rename and colour, chop, select source...)");
        else if (onHeader && ! m.ctrl)
            hint.text << "  " << tr ("The icon at the top left opens the clip menu (make unique, rename...)");
        else if (h.target == gesture::Target::item && ! m.ctrl && automationEditable (h.clip)
                 && automationBody (song.playlist[(std::size_t) h.clip]).contains (p.toFloat())) // round 165
            hint.text << "  " << tr ("[RMB] adds a point here (hold and drag to place it)  [Shift][RMB] on the curve");
        else if (h.target == gesture::Target::item && ! m.ctrl)
            hint.text << "  " << tr ("[Shift][V] + drag: a unique copy  [Alt][RMB] hold: hear it");
        const auto& c = song.playlist[(std::size_t) h.clip];
        hint.hasMenu = true;
        if (c.kind == ClipKind::pattern && c.pattern >= 0 && c.pattern < (int) song.patterns.size())
            hint.value = juce::String::fromUTF8 (song.patterns[(std::size_t) c.pattern].name.c_str());
        else if (c.kind == ClipKind::audio && c.channel >= 0 && c.channel < (int) song.channels.size())
            hint.value = h.target == gesture::Target::gainHandle ? tr ("Gain: {0}", { gainText (c.gain) })
                                                                 : juce::String::fromUTF8 (song.channels[(std::size_t) c.channel].name.c_str());
        else if (c.kind == ClipKind::automation && c.automation >= 0 && c.automation < (int) song.automations.size())
            hint.value = juce::String::fromUTF8 (song.automations[(std::size_t) c.automation].name.c_str());
    }
    else
        hint.value = trackName (h.track);

    return hint;
}

void PlaylistComponent::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (laneAt (e.getPosition()).track >= 0) // round 177b: lanes handle their clicks in mouseDown
        return;

    if (trackZoomHandle().contains (e.getPosition())) // the vertical zoom handle: back to 100%
    {
        setTrackZoom (1.0, tracksTop());
        return;
    }

    // double-click a track divider: back to the default height
    if (const auto divider = dividerAt (e.getPosition()); divider >= 0)
    {
        setTrackHeights ({ divider }, trackHeight);
        return;
    }

    // double-click a track header: open the source of the track's first clip (FL); empty tracks: rename
    if (e.x < headerWidth && e.y >= tracksTop() && e.y < gridArea().getBottom() && e.mods.isLeftButtonDown())
    {
        const auto track = yToTrack (e.y);

        if (trackLed (track).expanded (3).contains (e.getPosition()) || trackArrow (track).contains (e.getPosition())
            || trackArm (track).expanded (2).contains (e.getPosition()) || trackInput (track).expanded (2).contains (e.getPosition()))
            return;
        if (const auto insert = simpleInsert (track); insert > 0)
        {
            // the Simple headers: a double-click resets the volume (to 0 dB) or the pan (centre); on the name it renames
            const auto p = e.getPosition();
            if (trackVolume (track).expanded (0, 4).contains (p))
                engine.editSong ([insert] (Song& s) { s.mixer[(std::size_t) insert].volume = unityFader; });
            else if (trackPan (track).expanded (2).contains (p))
                engine.editSong ([insert] (Song& s) { s.mixer[(std::size_t) insert].pan = 0.0f; });
            else if (! trackSolo (track).contains (p) && ! trackFx (track).contains (p))
                renameTrack (track);
            return;
        }

        const auto& playlist = engine.getSong().playlist;
        int first = -1;
        for (int i = 0; i < (int) playlist.size(); ++i)
            if (playlist[(std::size_t) i].track == track && (first < 0 || playlist[(std::size_t) i].start < playlist[(std::size_t) first].start))
                first = i;

        if (first >= 0)
            openClipSource (first, e.mods.isAltDown());
        else
            renameTrack (track);
        return;
    }

    bool edge = false;
    const auto hit = gridArea().contains (e.getPosition()) ? clipAt (e.x, e.y, edge) : -1;

    if (hit < 0)
        return;

    openClipSource (hit, e.mods.isAltDown());
}

void PlaylistComponent::openClipSource (int hit, bool other)
{
    if (hit < 0 || hit >= (int) engine.getSong().playlist.size())
        return;

    const auto clip = engine.getSong().playlist[(std::size_t) hit];

    if (clip.kind == ClipKind::pattern && clip.pattern != engine.getSong().currentPattern
        && clip.pattern < (int) engine.getSong().patterns.size())
        engine.editSong ([p = clip.pattern] (Song& s) { s.currentPattern = p; }, false);

    if (clip.kind == ClipKind::automation)
        openAutomationEditor (engine, clip.automation);
    else if (clip.kind == ClipKind::audio) // round 170h2: was nothing (the hint said "opens")
    {
        // FL's "Double-click on audio clips" (Manage shortcuts, ShortcutEditorForm AudioClipsPanel): the channel
        // settings, Alt = the clip properties (LYNX's Alt+double-click since round 153)
        if (other)
            showClipProperties (engine, hit, { hit }, [safe = juce::Component::SafePointer<PlaylistComponent> (this)] {
                if (safe != nullptr)
                    safe->sortClips();
            });
        else if (clip.channel >= 0 && clip.channel < (int) engine.getSong().channels.size())
            openChannelSettings (engine, clip.channel);
    }
    else if (clip.kind == ClipKind::pattern)
    {
        auto pianoRoll = editPatternsIn == 2
                         || (editPatternsIn == 0 && clip.pattern < (int) engine.getSong().patterns.size()
                             && hasPianoRollNotes (engine.getSong().patterns[(std::size_t) clip.pattern]));
        if (other)
            pianoRoll = ! pianoRoll;

        if (pianoRoll && onOpenPianoRoll)
            onOpenPianoRoll();
        else if (! pianoRoll && onOpenPattern)
            onOpenPattern();
    }
}

bool PlaylistComponent::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (SampleLibrary::isSampleFile (juce::File (f)) || videotracks::isVideoFile (juce::File (f))
            || (simpleHeaders && onSimpleFilesDropped && juce::File (f).hasFileExtension ("mid;midi")))
            return true;

    return false;
}

namespace
{
// An audio clip of `file` at `start` on `track` (one undo step); `reuse`: an existing audio-clip channel of the same
// file plays it instead of a new one. False when the file can't be read.
bool addAudioClip (AudioEngine& engine, const juce::File& file, std::int64_t start, int track, bool reuse, bool loopsKeepTime = false)
{
    if (! SampleLibrary::isSampleFile (file))
        return false;

    const auto data = engine.getSampleLibrary().load (file.getFullPathName());

    if (data == nullptr)
        return false;

    // audio length converted to ticks at the current BPM
    const auto seconds = (double) data->frames() / data->sampleRate;
    auto length = juce::jmax<std::int64_t> (ppq / 4, (std::int64_t) (seconds * engine.getSong().bpm / 60.0 * ppq));
    const auto path = file.getFullPathName().toStdString();
    const auto name = file.getFileNameWithoutExtension().toStdString();
    // the Simple workflow (round 190p): a loop made for a tempo lasts its beats at the project's tempo, at its own pitch
    const auto loopBeats = loopsKeepTime ? simpletracks::loopBeats (file.getFileName().toStdString(), seconds, data->acidBeats, engine.getSong().beatsPerBar)
                                         : 0.0f;
    if (loopBeats > 0.0f)
        length = juce::jmax<std::int64_t> (1, (std::int64_t) std::llround ((double) loopBeats * ppq));

    engine.editSong ([=] (Song& s) {
        int channel = -1;
        if (reuse)
            for (int c = 0; c < (int) s.channels.size(); ++c)
                if (s.channels[(std::size_t) c].instrument == InstrumentKind::audioClip && s.channels[(std::size_t) c].samplePath == path)
                    channel = c;

        if (channel < 0)
        {
            ChannelDef c;
            c.name = name;
            c.instrument = InstrumentKind::audioClip;
            c.sampler.declickMode = audioClipDeclickMode; // FL's AudioTrack.fst
            c.samplePath = path;
            // dropped on an audio track: the new channel plays through its mixer track (FL [DECOMP FUN_012f2a10])
            const auto linked = trackmodes::armableInsert (s, track);
            c.mixerTrack = linked > 0 ? linked : s.claimFreeInsert();
            if (loopBeats > 0.0f)
            {
                c.sampler.stretchBeats = loopBeats;
                c.sampler.keepPitch = true;
            }
            s.channels.push_back (c);
            channel = (int) s.channels.size() - 1;
        }

        Clip clip;
        clip.kind = ClipKind::audio;
        clip.channel = channel;
        clip.track = track;
        clip.start = start;
        clip.length = length;
        clip.flFadeFlags = newClipFadeFlags();
        s.playlist.push_back (clip);
        trackmodes::followAudioTracks (s, { (int) s.playlist.size() - 1 }); // (a reused channel)
        s.mode = PlayMode::song;
    });
    return true;
}
} // namespace

void PlaylistComponent::filesDropped (const juce::StringArray& files, int x, int y)
{
    // on a track header (round 189e): FL's Edit › Drop audio on track headers, on an unassigned track
    if (x < headerWidth && y >= tracksTop() && y < gridArea().getBottom())
        if (const auto track = yToTrack (y); track >= 0 && (track >= (int) engine.getSong().playlistTracks.size()
                                                             || engine.getSong().playlistTracks[(std::size_t) track].mode == PlaylistTrackMode::unassigned))
        {
            dropOnTrackHeader (files, track);
            return;
        }

    auto start = juce::jmax<std::int64_t> (0, snap (xToTick (x), true));
    auto track = yToTrack (y);

    // the Simple workflow (round 184a): files dropped on the lane under the tracks (or on any track that isn't one of
    // its tracks), and MIDI files anywhere, become new tracks of their own
    if (simpleHeaders && onSimpleFilesDropped)
    {
        const auto midi = std::any_of (files.begin(), files.end(), [] (const juce::String& f) { return juce::File (f).hasFileExtension ("mid;midi"); });
        if (midi || ! simpletracks::kindOf (engine.getSong(), track).has_value())
        {
            onSimpleFilesDropped (files, x >= headerWidth ? start : 0);
            return;
        }
    }

    for (const auto& f : files)
        if (videotracks::isVideoFile (juce::File (f)))
            // a video dropped on the clip tracks: a video clip on a video track, where it was dropped (round 174d)
            videotracks::importVideo (engine, juce::File (f), engine.getSong().secondsForTicks (start));
        else if (addAudioClip (engine, juce::File (f), start, track, false, simpleHeaders)) // (Simple: a loop keeps time)
            ++track; // multiple files: one below the other
}

void PlaylistComponent::addAudioClipAtPlayhead (const juce::File& file)
{
    const auto& song = engine.getSong();
    const auto start = song.mode == PlayMode::song ? juce::jmax<std::int64_t> (0, snap ((std::int64_t) engine.getTransport().positionTicks.load(), false))
                                                   : std::int64_t (0);
    // the first track with nothing at the start
    int track = 0;
    for (;; ++track)
    {
        const auto busy = std::any_of (song.playlist.begin(), song.playlist.end(), [&] (const Clip& c) {
            return c.track == track && c.start <= start && c.end() > start;
        });
        if (! busy)
            break;
    }
    addAudioClip (engine, file, start, track, true);
}

void PlaylistComponent::glideBy (double ticks, double pixels, bool immediate)
{
    if (! gliding)
    {
        glideX.jumpTo (scrollTicks);
        glideY.jumpTo (scrollY);
        gliding = true;
    }

    const auto grid = gridArea();
    glideX.target = juce::jlimit (0.0, juce::jmax (0.0, hBar.getMaximumRangeLimit() - grid.getWidth() / pixelsPerTick), glideX.target + ticks);
    if (ticks != 0.0) // (a whole number of pixels from the start: the cached picture shifts, round 174e2)
        glideX.target = std::round (glideX.target * pixelsPerTick) / pixelsPerTick;
    glideY.target = juce::jlimit (0.0, juce::jmax (0.0, (double) tracksHeight() - grid.getHeight()), glideY.target + pixels);
    if (immediate || ! ui::motion::enabled()) // (Settings › General › Animations off: the wheel jumps)
    {
        scrollTicks = glideX.target;
        scrollY = (int) std::round (glideY.target);
        gliding = false;
        updateScrollBars();
        glideX.jumpTo (scrollTicks); // the scroll bar may have clamped it
        glideY.jumpTo (scrollY);
        repaintScrolled();
        return;
    }
    vblank.start(); // the next frames at the display's refresh (round 190gv: no vertical blanks while still)
    glideFrame();
}

void PlaylistComponent::glideFrame()
{
    if (! gliding)
        return;

    // someone else moved the view (scroll bar, zoom, pan, playback follow): the glide gives way
    if (std::abs (scrollTicks - glideX.current) > 1e-6 || std::abs ((double) scrollY - std::round (glideY.current)) > 0.5)
    {
        gliding = false;
        return;
    }

    // whole pixels sideways (round 174e2: the cached picture then shifts instead of being painted again), at least one
    // pixel towards the target each frame. Only when it is moving sideways: a zoom can leave the view between two pixels,
    // and a plain wheel there flipped it by that fraction every frame, each frame painting everything (round 174f)
    if (glideX.isMoving())
    {
        const auto fromPixels = scrollTicks * pixelsPerTick;
        const auto steppedPixels = glideX.step (0.45, 0.5 / pixelsPerTick) * pixelsPerTick;
        scrollTicks = (glideX.target * pixelsPerTick > fromPixels ? std::ceil (steppedPixels - 1.0e-6) : std::floor (steppedPixels + 1.0e-6)) / pixelsPerTick;
        if ((glideX.target - scrollTicks) * (glideX.target * pixelsPerTick - fromPixels) < 0.0) // (never past the target)
            scrollTicks = glideX.target;
    }
    scrollY = (int) std::round (glideY.step (0.45, 1.0));
    glideY.current = scrollY;
    const auto wanted = scrollTicks;
    updateScrollBars();
    if (std::abs (scrollTicks - wanted) > 1.0e-6) // (the scroll bar clamped it: this is as far as it goes)
        glideX.target = scrollTicks;
    glideX.current = scrollTicks;
    gliding = glideX.isMoving() || glideY.isMoving();
    repaintScrolled();
}

void PlaylistComponent::mouseMagnify (const juce::MouseEvent& e, float scale)
{
    gliding = false;
    const auto factor = gesture::pinchZoomFactor (scale);
    if (e.mods.isAltDown()) // the track heights, around the pointer
    {
        setTrackZoom (trackZoom * factor, e.y);
        return;
    }
    const auto anchorPixels = (double) juce::jmax (0, e.x - headerWidth);
    const auto zoomed = juce::jlimit (4.0 / (4 * ppq), 800.0 / (4 * ppq), pixelsPerTick * factor);
    scrollTicks = gesture::zoomAnchored (scrollTicks, pixelsPerTick, zoomed, anchorPixels);
    pixelsPerTick = zoomed;
    zoomBeforeAll = 0.0;
    updateScrollBars();
    repaint();
}

void PlaylistComponent::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& rawWheel)
{
    const auto wheel = flmods::normalisedWheel (rawWheel); // Windows touchpads: pieces of a notch (round 170e)
    const auto ctrl = flmods::ctrl (e.mods), alt = e.mods.isAltDown(), shift = e.mods.isShiftDown();

    // Shift+wheel over a track header: moves the track up / down
    if (shift && ! ctrl && e.x < headerWidth && e.y >= tracksTop() && wheel.deltaY != 0.0f)
    {
        const auto track = yToTrack (e.y);
        const auto other = track + (wheel.deltaY > 0 ? -1 : 1);

        if (other >= 0 && other < numTracks())
        {
            setSelection ({});
            engine.editSong ([track, other] (Song& s) { noteedit::swapTracks (s, track, other); });
            // the mouse follows the track, so the next wheel step moves it further
            refreshTrackLayout();
            juce::Desktop::setMousePosition (localPointToGlobal (juce::Point<int> (e.x, trackToY (other) + heightOf (other) / 2)));
        }
        return;
    }

    // FL's Edit › "Nudge clips with mouse wheel" (on by default; FUN_00e82270): Shift+wheel shifts the clips' content
    if (shift && ! ctrl && ! alt && EditorGlobals::get().wheelNudging && gridArea().contains (e.getPosition()) && wheel.deltaY != 0.0f)
        if (nudgeWithWheel (e.getPosition(), wheel.deltaY))
            return;

    if (shift && alt) // nudge the selected clips by the grid
    {
        if (wheel.deltaY != 0.0f && ! selection.empty())
        {
            const auto now = juce::Time::getMillisecondCounter();
            const auto newBurst = now - lastWheelEdit > 700; // one undo step per burst
            lastWheelEdit = now;
            moveClips (selection, (wheel.deltaY > 0 ? 1 : -1) * gridTicks(), 0, newBurst, false); // (shared with Shift+arrows)
        }
        return;
    }

    if (alt && ! shift) // Alt+wheel (and Ctrl+Alt+wheel, as in the Piano Roll) zooms vertically: the track heights, around the mouse
    {
        gliding = false;
        setTrackZoom (trackZoom * gesture::wheelZoomFactor (wheel.deltaY, wheel.isSmooth), e.y);
        return;
    }

    if (ctrl)
    {
        gliding = false;
        const auto anchorPixels = (double) juce::jmax (0, e.x - headerWidth);
        const auto zoomed = juce::jlimit (4.0 / (4 * ppq), 800.0 / (4 * ppq), pixelsPerTick * gesture::wheelZoomFactor (wheel.deltaY, wheel.isSmooth));
        scrollTicks = gesture::zoomAnchored (scrollTicks, pixelsPerTick, zoomed, anchorPixels);
        pixelsPerTick = zoomed;
        zoomBeforeAll = 0.0;
        updateScrollBars();
        repaintZoomed();
        return;
    }

    // plain wheel: up/down (Shift: left/right); a horizontal wheel or trackpad swipe scrolls sideways (a trackpad at once)
    if (shift)
        glideBy (gesture::wheelScrollPixels (wheel.deltaY, wheel.isSmooth, 120.0) / pixelsPerTick, 0.0, wheel.isSmooth);
    else
        glideBy (gesture::wheelScrollPixels (wheel.deltaX, wheel.isSmooth, 80.0) / pixelsPerTick,
                 gesture::wheelScrollPixels (wheel.deltaY, wheel.isSmooth, 2.0 * defaultRowHeight()), wheel.isSmooth);
}

bool PlaylistComponent::nudgeWithWheel (juce::Point<int> p, float deltaY)
{
    // FL (FUN_00e82270, checked in FL 2026: "playlist shift clip" in the undo history): Shift+wheel shifts the CONTENT of
    // the selected clips, or with none selected of the clip under the mouse, inside the clips' bounds: a notch is one
    // pixel's worth of ticks at the current zoom (the PLForm's +0xb04, rounded), up = the content moves earlier. Over
    // an empty part of the grid nothing happens and the wheel is still taken. One undo step per burst of the wheel.
    const auto& song = engine.getSong();
    std::vector<int> targets = selection;
    if (targets.empty())
    {
        bool right = false, left = false;
        if (const auto hit = clipAt (p.x, p.y, right, left); hit >= 0 && song.playlist[(std::size_t) hit].kind != ClipKind::automation)
            targets = { hit };
        else
            return true;
    }
    const auto delta = (deltaY > 0 ? 1 : -1) * clipops::pixelStepTicks (pixelsPerTick);
    const auto now = juce::Time::getMillisecondCounter();
    const auto newBurst = now - lastWheelEdit > 700;
    lastWheelEdit = now;
    engine.editSong ([targets, delta] (Song& s) { clipops::shiftClipContent (s.playlist, targets, delta); }, newBurst);
    return true;
}

void PlaylistComponent::scrollBarMoved (juce::ScrollBar* bar, double start)
{
    if (bar == &hBar)
        scrollTicks = start;
    else
        scrollY = (int) start;

    repaintScrolled();
}

void PlaylistComponent::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == ui::WaveformCache::getInstanceWithoutCreating())
    {
        repaint();
        return;
    }

    // the clip list changed externally (undo, pattern removal...): the selection no longer applies
    const auto count = engine.getSong().playlist.size();

    if (count != knownClipCount && drag == Drag::none)
    {
        knownClipCount = count;
        selection.clear();
    }

    // switching pattern (top bar, Channel Rack) sets the pencil back to the pattern
    if (engine.getSong().currentPattern != knownCurrentPattern)
    {
        knownCurrentPattern = engine.getSong().currentPattern;
        brush = {};
    }

    followButton.setToggleState (EditorGlobals::get().isFollowing (false), juce::dontSendNotification);
    refreshArrangementBox();
    refreshTrackLayout();
    if (engine.getSong().patternBlocksOn != blocksShown) // Pattern Blocks turned on / off (menu, undo, another project)
        resized();
    if (videoSectionHeight() != videoLanes->getHeight()) // video tracks added / removed / resized (round 174d)
        resized();
    blocksView->refresh();
    videoLanes->refresh();
    updateScrollBars();
    repaint();
}

bool PlaylistComponent::liveFrame (double nowMs)
{
    auto& transport = engine.getTransport();
    const auto& song = engine.getSong();
    const auto playing = transport.playing.load();
    auto tick = -1.0;

    // (between two audio blocks the playhead moves on at the song's tempo: smooth at 144 Hz, round 174e)
    if (song.mode == PlayMode::song && (playing || transport.positionTicks.load() > 0.0))
        tick = engine.songPlayheadTicks() + engine.playheadAdvanceTicks();

    // whenever it moved: the line is anti-aliased, so even a fraction of a pixel shows (zoomed out, a playhead crossing
    // a few pixels a second glides at the display's rate instead of stepping, as FL's does)
    const auto moved = (tick < 0.0) != (playheadTick < 0.0) || std::abs ((tick - playheadTick) * pixelsPerTick) >= 0.002;
    if (moved && isShowing())
    {
        // only the cursor column (old and new) is repainted: large projects stay cheap. One rectangle for both: the
        // two strips overlap (a frame moves the line by a pixel or less), and each rectangle of a repaint is a paint
        auto column = [this] (double t) {
            return t >= 0.0 ? juce::Rectangle<int> ((int) tickToX (t) - 8, 0, 16, getHeight()) : juce::Rectangle<int>();
        };
        auto dirty = [this] (juce::Rectangle<int> r) {
            if (r.isEmpty())
                return;
            playheadRepaintOnly = playheadOverlay; // (the cached image under it stays valid)
            repaint (r);
            playheadRepaintOnly = false;
        };

        const auto before = column (playheadTick);
        playheadTick = tick;

        // Follow: if the cursor leaves the screen while playing, scrolls one page (off: the view stays put)
        const auto grid = gridArea();
        const auto newStart = transportedit::followViewStart (scrollTicks, grid.getWidth() / pixelsPerTick, tick, 20.0 / pixelsPerTick,
                                                              20.0 / pixelsPerTick);

        if (EditorGlobals::get().isFollowing (false) && playing && newStart != scrollTicks)
        {
            scrollTicks = newStart;
            updateScrollBars();
            repaint();
        }
        else if (const auto after = column (tick); ! before.isEmpty() && ! after.isEmpty() && std::abs (after.getX() - before.getX()) <= 16)
            dirty (before.getUnion (after));
        else
        {
            dirty (before);
            dirty (after);
        }
    }
    else if (moved)
        playheadTick = tick;

    // Performance mode: the launched and waiting clips (frames, progress) and the headers' track progress
    auto live = false;
    if (song.performanceMode && isShowing())
    {
        const auto& perfState = engine.performanceState();
        const auto zoneEnd = song.performanceZoneEnd();
        auto changed = false;
        for (int t = 0; t < (int) perfShown.size(); ++t)
        {
            const auto v = perfState.tracks[(std::size_t) t].playing.load (std::memory_order_relaxed) * 2 + 1
                           + (perfState.tracks[(std::size_t) t].pending.load (std::memory_order_relaxed) + 1) * 7919;
            if (v != perfShown[(std::size_t) t])
            {
                perfShown[(std::size_t) t] = v;
                changed = true;
            }
        }
        live = perfState.anyPlaying.load (std::memory_order_relaxed);
        // (the clips' progress and the waiting clips' blink cover the whole zone: 30 times a second, as before)
        const auto due = live && nowMs - perfRepaintMs >= 1000.0 / 30.0;
        if (zoneEnd > 0 && (changed || due))
        {
            perfRepaintMs = nowMs;
            const auto grid = gridArea();
            const auto x1 = juce::jmin (grid.getRight(), (int) tickToX ((double) zoneEnd) + 2);
            if (x1 > grid.getX())
                repaint (juce::Rectangle<int> (grid.getX(), grid.getY(), x1 - grid.getX(), grid.getHeight()));
            if (showTrackProgress)
                repaint (0, tracksTop(), headerWidth, getHeight() - tracksTop());
        }
    }
    // round 190w: the track headers' level meters
    if (stepHeaderMeters (nowMs))
        live = true;

    // round 177d: a recording in progress grows on its tracks (its rows, from where it starts to the playhead; 30 Hz)
    if (engine.isLiveRecording() && isShowing())
    {
        if (nowMs - liveRepaintMs >= 1000.0 / 30.0)
        {
            liveRepaintMs = nowMs;
            const auto rec = engine.liveRecording();
            juce::Rectangle<float> area;
            for (const auto& a : rec.audio)
                area = area.getUnion (liveBounds (a.track, a.span.start, a.span.start + a.span.length).expanded (2.0f, 1.0f));
            if (rec.notes && rec.track >= 0)
                area = area.getUnion (liveBounds (rec.track, (double) rec.from, (double) rec.to + 16.0 / pixelsPerTick).expanded (2.0f, 1.0f));
            const auto now = area.getSmallestIntegerContainer();
            const auto dirty = now.getUnion (liveArea).getIntersection (gridArea());
            liveArea = now;
            if (! dirty.isEmpty())
                repaint (dirty);
        }
        return true;
    }
    if (! liveArea.isEmpty())
    {
        repaint (liveArea);
        liveArea = {};
    }
    return playing || moved || live;
}

// Round 190w: FL 2026's track header levels (Playlist menu › View › Track headers [DFM] EEShowAudioTrackMetersAction
// Checked=true, EEShowInstrumentTrackMetersAction unchecked; [SHOT] flgap190w/shots/fl p1-p3, b4, c4: a stereo peak
// meter with hold lines in a narrow column right of the header, on every row height).
int PlaylistComponent::meteredInsertOf (int track) const
{
    trackmodes::HeaderOptions o;
    o.audioControls = showAudioTrackControls;
    o.audioLevels = showAudioTrackLevels;
    o.instrumentLevels = showInstrumentTrackLevels;
    if (noteedit::isFoldedAway (engine.getSong(), track))
        return -1;
    return trackmodes::meteredInsert (engine.getSong(), track, o);
}

bool PlaylistComponent::updateMeterColumn()
{
    auto any = false;
    if (showAudioTrackLevels || showInstrumentTrackLevels)
        for (int t = 0, n = (int) engine.getSong().playlistTracks.size(); t < n && ! any; ++t)
            any = meteredInsertOf (t) >= 0;
    const auto width = any ? 9 : 0;
    if (width == meterColumn)
        return false;
    meterColumn = width;
    if (! any)
        headerLevels.clear();
    return true;
}

bool PlaylistComponent::stepHeaderMeters (double nowMs)
{
    if (updateMeterColumn())
        repaint (0, tracksTop(), headerWidth, getHeight() - tracksTop());
    if (meterColumn == 0 || ! isShowing())
    {
        headerMeterMs = -1.0;
        return false;
    }
    const auto elapsed = headerMeterMs < 0.0 ? 1000.0 / 60.0 : juce::jlimit (0.0, 1000.0, nowMs - headerMeterMs);
    headerMeterMs = nowMs;

    auto& bank = engine.getMeters();
    const auto grid = gridArea();
    auto moving = false;
    std::map<int, std::array<meters::PeakHold, 2>> next;
    juce::Rectangle<int> dirty;
    for (int t = 0, n = numTracks(); t < n; ++t)
    {
        const auto insert = meteredInsertOf (t);
        if (insert < 0)
            continue;
        if (next.find (insert) == next.end())
        {
            auto levels = headerLevels.count (insert) > 0 ? headerLevels[insert] : std::array<meters::PeakHold, 2> {};
            for (int side = 0; side < 2; ++side)
                levels[(std::size_t) side].advance (bank.takeViewPeak (insert, side), elapsed);
            moving = moving || levels[0].level > 0.0f || levels[1].level > 0.0f || levels[0].peak > 0.0f || levels[1].peak > 0.0f;
            next[insert] = levels;
        }
        const auto y = trackToY (t), h = heightOf (t);
        if (y + h >= grid.getY() && y <= grid.getBottom())
        {
            const auto& before = headerLevels.count (insert) > 0 ? headerLevels[insert] : std::array<meters::PeakHold, 2> {};
            const auto& after = next[insert];
            if (before[0].level != after[0].level || before[1].level != after[1].level || before[0].peak != after[0].peak
                || before[1].peak != after[1].peak)
                dirty = dirty.getUnion (juce::Rectangle<int> (headerWidth - meterColumn, y, meterColumn, h));
        }
    }
    headerLevels = std::move (next);
    if (! dirty.isEmpty())
        repaint (dirty.getIntersection (juce::Rectangle<int> (0, tracksTop(), headerWidth, getHeight() - tracksTop())));
    return moving;
}

void PlaylistComponent::headerOptionsChanged()
{
    updateMeterColumn();
    repaint();
    if (onViewOptionChanged)
        onViewOptionChanged();
}

// Round 177d: the recording in progress, live (MusicTech's FL 2025 review: recorded MIDI is not shown in the Playlist
// while recording, audio recording "unintuitive"). Each audio take is a growing clip with its waveform on the track it
// will land on; a Song-mode note take is a frame over the range it covers so far (its clip, placed with the first
// released note, and where it will grow to), with the notes still held. At the stop they become the real clips.
juce::Rectangle<float> PlaylistComponent::liveBounds (int track, double from, double to) const
{
    if (track < 0 || track >= numTracks() || to <= from)
        return {};
    const auto x0 = (float) tickToX (from), x1 = (float) tickToX (to);
    return { x0, (float) trackToY (track) + 1.0f, juce::jmax (3.0f, x1 - x0 - 1.0f), (float) heightOf (track) - 3.0f };
}

void PlaylistComponent::paintLiveRecording (juce::Graphics& g)
{
    using namespace theme;
    const auto live = engine.liveRecording();
    const auto& song = engine.getSong();
    const auto visible = g.getClipBounds().toFloat();

    for (const auto& a : live.audio)
    {
        auto from = a.span.start, to = a.span.start + a.span.length;
        if (live.punch.has_value()) // (only the punch range is kept)
        {
            from = juce::jmax (from, (double) live.punch->in);
            to = juce::jmin (to, (double) live.punch->out);
        }
        const auto r = liveBounds (a.track, from, to);
        if (r.isEmpty() || ! r.intersects (visible))
            continue;
        const auto body = paintClipFrame (g, r, colour::accent, a.name, ClipGlyph::audio);
        const auto wave = body.reduced (0.0f, body.getHeight() >= 14.0f ? 2.0f : 1.0f).getIntersection (visible);
        if (a.peaks == nullptr || wave.getWidth() < 1.0f || wave.getHeight() < 2.0f || a.span.ticksPerSample <= 0.0)
            continue;
        // one value per pixel column, read under one lock
        const auto columns = (int) std::ceil (wave.getWidth());
        liveColumns.resize ((std::size_t) columns);
        const auto framesPerPixel = 1.0 / (pixelsPerTick * a.span.ticksPerSample);
        const auto firstFrame = a.span.firstFrame + (xToTick ((int) wave.getX()) - a.span.start) / a.span.ticksPerSample;
        a.peaks->read (firstFrame, framesPerPixel, columns, liveColumns.data());
        juce::RectangleList<float> bars;
        const auto mid = wave.getCentreY(), half = wave.getHeight() * 0.5f;
        for (int k = 0; k < columns; ++k)
            if (const auto h = juce::jmin (1.0f, liveColumns[(std::size_t) k]) * half; h > 0.0f)
                bars.addWithoutMerging ({ wave.getX() + (float) k, mid - h, 1.0f, juce::jmax (1.0f, 2.0f * h) });
        g.setColour (clipContentColour (colour::accent));
        g.fillRectList (bars);
        g.fillRect (wave.getX(), mid - 0.5f, wave.getWidth(), 1.0f); // the zero line, as far as the take has come
    }

    if (live.notes && live.track >= 0 && live.to > live.from)
    {
        const auto r = liveBounds (live.track, (double) live.from, (double) live.to);
        if (r.isEmpty() || ! r.intersects (visible))
            return;
        g.setColour (colour::accent.withAlpha (0.10f));
        g.fillRoundedRectangle (r, radius::sm);
        g.setColour (colour::accent.withAlpha (0.85f));
        g.drawRoundedRectangle (r.reduced (0.5f), radius::sm, 1.5f);
        if (live.held.empty() || live.pattern < 0 || live.pattern >= (int) song.patterns.size())
            return;
        // the held notes at their pitch, in the pattern's key range (as the clip draws its notes)
        int low = 127, high = 0;
        for (const auto& n : song.patterns[(std::size_t) live.pattern].notes)
            low = juce::jmin (low, n.key), high = juce::jmax (high, n.key);
        for (const auto& n : live.held)
            low = juce::jmin (low, n.key), high = juce::jmax (high, n.key);
        const auto inner = r.withTrimmedTop (clipHeaderHeight (r)).reduced (0.0f, 3.0f);
        const auto span = juce::jmax (6, high - low + 2);
        const auto bottomKey = (low + high) / 2.0f - span / 2.0f;
        const auto rows = (float) span + 1.0f;
        const auto noteHeight = juce::jlimit (1.5f, 3.0f, inner.getHeight() / rows);
        g.setColour (colour::accent.brighter (0.35f));
        for (const auto& n : live.held)
        {
            const auto at = (double) (live.origin + n.start);
            const auto nx = (float) tickToX (at);
            const auto nw = juce::jmax (2.0f, (float) (n.length * pixelsPerTick) - 1.0f);
            const auto ny = inner.getBottom() - ((float) n.key - bottomKey + 0.5f) / rows * inner.getHeight() - noteHeight * 0.5f;
            g.fillRect (nx, ny, nw, noteHeight);
        }
    }
}

// Performance mode (round 165m): the launched clips (a bright frame and, with "Performance clip progress", how far the
// pass has gone), the clips waiting for their sync point (a blinking frame) and, with "Performance track progress",
// the playing clip's progress on the track header.
void PlaylistComponent::paintPerformance (juce::Graphics& g, juce::Rectangle<int> grid)
{
    using namespace theme;
    const auto& song = engine.getSong();
    const auto& live = engine.performanceState();
    const auto zoneEnd = song.performanceZoneEnd();
    const auto any = live.anyPlaying.load (std::memory_order_relaxed);
    const auto blink = 0.45f + 0.45f * std::sin ((float) juce::Time::getMillisecondCounter() * 0.012f);
    for (int i = 0; i < (int) song.playlist.size(); ++i)
    {
        const auto& c = song.playlist[(std::size_t) i];
        if (c.start >= zoneEnd || c.track < 0 || c.track >= perf::maxTracks)
            continue;
        const auto& view = live.tracks[(std::size_t) c.track];
        const auto playing = any && view.playing.load (std::memory_order_relaxed) == c.start;
        const auto pending = any && view.pending.load (std::memory_order_relaxed) == c.start;
        const auto r = clipBounds (i).toFloat();
        if (! r.intersects (grid.toFloat()))
            continue;
        if (! playing && ! pending)
        {
            // as in FL, the zone's idle clips are dimmed: what plays stands out
            g.setColour (colour::bg0.withAlpha (0.42f));
            g.fillRoundedRectangle (r, radius::sm);
            continue;
        }
        if (playing)
        {
            // a play mark at its left edge (FL: ▸) besides the bright frame
            juce::Path mark;
            const auto mx = r.getX() + 5.0f, my = r.getCentreY() + 3.0f;
            mark.addTriangle (mx, my - 4.0f, mx, my + 4.0f, mx + 6.0f, my);
            g.setColour (colour::ledOn);
            g.fillPath (mark);
            g.setColour (colour::ledOn);
            g.drawRoundedRectangle (r.reduced (1.0f), radius::sm, 2.0f);
            if (showClipProgress)
            {
                // as in FL, the part already played is lighter, up to a thin playhead line
                const auto p = juce::jlimit (0.0f, 1.0f, view.progress.load (std::memory_order_relaxed));
                const auto played = r.reduced (1.0f).withWidth (juce::jmax (0.0f, (r.getWidth() - 2.0f) * p));
                g.setColour (colour::textPrimary.withAlpha (0.16f));
                g.fillRect (played);
                g.setColour (colour::ledOn.withAlpha (0.85f));
                g.fillRect (played.withX (played.getRight() - 1.0f).withWidth (1.5f));
            }
        }
        if (pending)
        {
            g.setColour (colour::warning.withAlpha (blink));
            g.drawRoundedRectangle (r.reduced (1.0f), radius::sm, 2.0f);
        }
    }
    (void) showTrackProgress; // (drawn on the headers: paintOverChildren)
}

void PlaylistComponent::togglePerformanceMode()
{
    const auto on = ! engine.getSong().performanceMode;
    engine.editSong ([on] (Song& s) { s.performanceMode = on; });
    if (on && engine.getSong().performanceZoneEnd() <= 0)
        ui::postHint ({ tr ("Performance mode: put a Start marker where the song starts (right-click the ruler); the clips before it are launched with a click while playing"), {} }, 6000);
    repaint();
}

void PlaylistComponent::zoomOnPerformanceClips()
{
    const auto end = engine.getSong().performanceZoneEnd();
    if (end > 0)
        zoomToTicks (0.0, (double) end);
}

bool PlaylistComponent::isInterestedInDragSource (const SourceDetails& details)
{
    int gridTrack = 0, gridScene = 0;
    return BrowserComponent::isBrowserDrag (details) || BrowserComponent::draggedPattern (details) >= 0
           || clipGridDrag (details.description, gridTrack, gridScene);
}

void PlaylistComponent::itemDropped (const SourceDetails& details)
{
    // a clip dragged out of the clip grid (round 190bv): a copy of it where it is dropped, one pass long
    if (int gridTrack = 0, gridScene = 0; clipGridDrag (details.description, gridTrack, gridScene))
    {
        const auto start = juce::jmax<std::int64_t> (0, snap (xToTick (details.localPosition.x), true));
        const auto track = juce::jmax (0, yToTrack (details.localPosition.y));
        engine.editSong ([=] (Song& s) { session::toPlaylist (s, gridTrack, gridScene, track, start); });
        return;
    }

    // a pattern from the Browser's "Current project": a clip of it where it is dropped
    if (const auto pattern = BrowserComponent::draggedPattern (details); pattern >= 0 && pattern < (int) engine.getSong().patterns.size())
    {
        const auto start = juce::jmax<std::int64_t> (0, snap (xToTick (details.localPosition.x), true));
        const auto track = juce::jmax (0, yToTrack (details.localPosition.y));
        engine.editSong ([=] (Song& s) {
            Clip clip;
            clip.kind = ClipKind::pattern;
            clip.pattern = pattern;
            clip.track = track;
            clip.start = start;
            clip.length = juce::jmax<std::int64_t> (ppq, s.patterns[(std::size_t) pattern].effectiveLength (s.beatsPerBar));
            s.playlist.push_back (clip);
            s.mode = PlayMode::song;
        });
        return;
    }

    if (const auto file = BrowserComponent::draggedFile (details); file.existsAsFile())
        filesDropped ({ file.getFullPathName() }, details.localPosition.x, details.localPosition.y);
}
// ---------------------------------------------------------------------------

PatternPicker::PatternPicker (AudioEngine& e, PlaylistComponent& p)
    : engine (e),
      playlist (p)
{
    engine.addChangeListener (this);
    seenBrush = playlist.brush.kind;
}

PatternPicker::~PatternPicker()
{
    engine.removeChangeListener (this);
}

void PatternPicker::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // the pencil's source changed elsewhere (a clip clicked, the track menu): show its kind, as FL does
    if (playlist.brush.kind != seenBrush)
    {
        seenBrush = playlist.brush.kind;
        view = seenBrush == ClipKind::audio ? 1 : seenBrush == ClipKind::automation ? 2 : 0;
        scroll = 0;
    }
    repaint();
}

void PatternPicker::setView (int newView)
{
    view = juce::jlimit (0, 2, newView);
    scroll = 0;
    repaint();
}

std::vector<PatternPicker::Row> PatternPicker::rows() const
{
    const auto& song = engine.getSong();
    std::vector<Row> list;

    if (view == 0)
        for (int i = 0; i < (int) song.patterns.size(); ++i)
            list.push_back ({ ClipKind::pattern, i, juce::String::fromUTF8 (song.patterns[(std::size_t) i].name.c_str()),
                              PlaylistComponent::patternColour (song, i) });

    if (view == 1)
        for (int c = 0; c < (int) song.channels.size(); ++c)
            if (song.channels[(std::size_t) c].instrument == InstrumentKind::audioClip)
                list.push_back ({ ClipKind::audio, c, juce::String::fromUTF8 (song.channels[(std::size_t) c].name.c_str()),
                                  PlaylistComponent::audioClipColour (song.channels[(std::size_t) c]) });

    if (view == 2)
        for (int a = 0; a < (int) song.automations.size(); ++a)
            list.push_back ({ ClipKind::automation, a, juce::String::fromUTF8 (song.automations[(std::size_t) a].name.c_str()), theme::colour::automation });

    return list;
}

juce::Rectangle<int> PatternPicker::tabBounds (int tab) const
{
    const auto w = (getWidth() - 8) / 3;
    return { 4 + tab * w, 2, w, headerHeight - 4 };
}

int PatternPicker::listBottom() const
{
    return view == 0 ? getHeight() - footerHeight : getHeight();
}

juce::Rectangle<int> PatternPicker::addBounds() const
{
    return view == 0 ? juce::Rectangle<int> (4, getHeight() - footerHeight + 2, getWidth() - 8, footerHeight - 4) : juce::Rectangle<int>();
}

int PatternPicker::rowAt (int y) const
{
    if (y < headerHeight || y >= listBottom())
        return -1;

    const auto index = (y - headerHeight) / rowHeight + scroll;
    return index < (int) rows().size() ? index : -1;
}

void PatternPicker::paint (juce::Graphics& g)
{
    const auto& song = engine.getSong();
    const auto list = rows();
    using namespace theme;
    g.fillAll (colour::bg2);
    g.setColour (colour::bg3);
    g.fillRect (0, 0, getWidth(), headerHeight);
    g.setColour (colour::border);
    g.fillRect (0, headerHeight - 1, getWidth(), 1);

    // the three tabs: patterns, audio clips, automation clips
    const Icon tabIcons[] = { Icon::pattern, Icon::audioFile, Icon::automate };
    for (int t = 0; t < 3; ++t)
    {
        const auto r = tabBounds (t).toFloat();
        if (t == view || t == hoverTab)
        {
            g.setColour (t == view ? colour::bg5 : colour::bg4);
            g.fillRoundedRectangle (r, radius::sm);
        }
        icons::draw (g, tabIcons[t], r.withSizeKeepingCentre (16.0f, 16.0f), t == view ? colour::accent : t == hoverTab ? colour::textPrimary : colour::textSecondary);
    }

    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (0, headerHeight, getWidth(), listBottom() - headerHeight);

        for (int i = scroll; i < (int) list.size(); ++i)
        {
            const auto y = headerHeight + (i - scroll) * rowHeight;

            if (y > listBottom())
                break;

            const auto& row = list[(std::size_t) i];
            const auto r = juce::Rectangle<int> (4, y + 2, getWidth() - 8, rowHeight - 4);
            const auto current = row.kind == ClipKind::pattern ? (playlist.brush.kind == ClipKind::pattern && row.index == song.currentPattern)
                                                               : (playlist.brush.kind == row.kind && playlist.brush.index == row.index);
            const auto rowColour = tame (row.colour);
            const auto box = r.toFloat();
            auto fill = rowColour.interpolatedWith (colour::bg3, current ? 0.25f : 0.55f);
            if (i == hoverRow && ! current)
                fill = fill.interpolatedWith (colour::bg5, 0.35f);
            g.setColour (fill);
            g.fillRoundedRectangle (box, radius::sm);

            // a faint miniature of the pattern's notes behind the name (FL's picker shows the same)
            if (row.kind == ClipKind::pattern && row.index < (int) song.patterns.size())
            {
                const auto& pattern = song.patterns[(std::size_t) row.index];
                if (! pattern.notes.empty())
                {
                    int low = 127, high = 0;
                    for (const auto& n : pattern.notes)
                    {
                        low = juce::jmin (low, n.key);
                        high = juce::jmax (high, n.key);
                    }
                    const auto body = box.reduced (3.0f, 3.0f);
                    const auto span = (float) juce::jmax (6, high - low + 2);
                    const auto length = (double) juce::jmax<std::int64_t> (1, pattern.effectiveLength (song.beatsPerBar));
                    g.setColour (clipContentColour (rowColour).withAlpha (0.35f));
                    for (const auto& n : pattern.notes)
                    {
                        const auto nx = body.getX() + (float) (n.start / length) * body.getWidth();
                        const auto nw = juce::jmax (1.5f, (float) (n.length / length) * body.getWidth());
                        const auto ny = body.getBottom() - ((float) (n.key - low) + 1.0f) / span * body.getHeight();
                        g.fillRect (nx, ny, juce::jmin (nw, body.getRight() - nx), 1.5f);
                    }
                }
            }

            if (row.kind == ClipKind::pattern && picked.contains (row.index) && picked.items.size() > 1) // picked with others
            {
                g.setColour (colour::accent.withAlpha (0.8f));
                g.drawRoundedRectangle (box.reduced (0.5f), radius::sm, 1.2f);
            }

            if (current) // the current source: the accent strip
            {
                g.setColour (colour::accent);
                g.fillRoundedRectangle (box.withWidth (3.0f), 1.5f);
            }

            g.setColour (clipTitleText (fill));
            g.setFont (font (type::small, current));
            textcache::drawText (g, row.name, r.withTrimmedLeft (9).withTrimmedRight (4), juce::Justification::centredLeft, true);
        }
    }

    if (const auto add = addBounds(); ! add.isEmpty())
    {
        g.setColour (hoverAdd ? colour::bg4 : colour::bg3);
        g.fillRoundedRectangle (add.toFloat(), radius::sm);
        icons::draw (g, Icon::plus, add.toFloat().withSizeKeepingCentre (14.0f, 14.0f), hoverAdd ? colour::textPrimary : colour::textSecondary);
    }

    g.setColour (colour::border);
    g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());
}

void PatternPicker::mouseDown (const juce::MouseEvent& e)
{
    for (int t = 0; t < 3; ++t)
        if (tabBounds (t).contains (e.getPosition()))
        {
            setView (t);
            return;
        }

    if (addBounds().contains (e.getPosition()))
    {
        if (onNewPattern)
            onNewPattern();
        return;
    }

    const auto list = rows();
    const auto i = rowAt (e.y);

    if (i < 0)
        return;

    const auto& row = list[(std::size_t) i];
    playlist.brush = { row.kind, row.index };
    seenBrush = row.kind;

    if (row.kind == ClipKind::pattern && row.index != engine.getSong().currentPattern)
        engine.editSong ([p = row.index] (Song& s) { s.currentPattern = p; }, false);

    // FL's picker selection (PickerPanelMouseDown FUN_00e4d5d0, REA_rodada169k §2.2)
    pressedRow = -1;
    if (row.kind == ClipKind::pattern)
    {
        pickersel::clamp (picked, (int) engine.getSong().patterns.size());
        if (flmods::popup (e.mods))
            pickersel::rightDown (picked, row.index, flmods::ctrl (e.mods));
        else
        {
            deselectOnUp = pickersel::leftDownDeselects (picked, row.index, flmods::ctrl (e.mods));
            pressedRow = i;
        }
    }

    if (flmods::popup (e.mods) && row.kind == ClipKind::pattern && onPatternMenu)
        onPatternMenu();

    repaint();
}

juce::PopupMenu PatternPicker::pickerMenu()
{
    const auto& song = engine.getSong();
    const auto unused = unusedPatterns (song);
    std::size_t duplicates = 0;
    for (const auto& group : identicalPatterns (song))
        duplicates += group.size() - 1;
    const juce::Component::SafePointer<PatternPicker> safe (this);
    auto edited = [safe] {
        if (safe != nullptr)
        {
            safe->picked.clear(); // (the indices moved)
            safe->repaint();
        }
    };

    juce::PopupMenu menu, select, sort, sortMenu;
    menu.addSectionHeader (tr ("Tools")); // (FL: Playlist menu › Picker panel › Tools, checked in FL in round 177d)
    select.addItem (tr ("Patterns ({0})", { juce::String ((int) unused.size()) }), ! unused.empty(), false, [safe, unused] {
        if (safe == nullptr)
            return;
        safe->setView (0);
        safe->picked.clear();
        for (auto p : unused)
            safe->picked.add (p);
        safe->repaint();
    });
    menu.addSubMenu (tr ("Select unused"), select);
    sortMenu.addItem (tr ("By name"), song.patterns.size() > 1, false, [this, edited] {
        engine.editSongPatternOrder ([] (Song& s) { sortPatterns (s, PatternSort::name); });
        edited();
    });
    sortMenu.addItem (tr ("By color"), song.patterns.size() > 1, false, [this, edited] {
        engine.editSongPatternOrder ([] (Song& s) { sortPatterns (s, PatternSort::colour); });
        edited();
    });
    sort.addSubMenu (tr ("Patterns"), sortMenu);
    menu.addSubMenu (tr ("Sort"), sort);
    menu.addSeparator();
    // LYNX's own (not in FL)
    menu.addItem (tr ("Delete unused patterns ({0})", { juce::String ((int) unused.size()) }), ! unused.empty() && unused.size() < song.patterns.size(), false,
                  [this, edited] {
                      engine.editSong ([] (Song& s) { deletePatterns (s, unusedPatterns (s)); });
                      edited();
                  });
    menu.addItem (tr ("Merge identical patterns ({0})", { juce::String ((int) duplicates) }), duplicates > 0, false, [this, edited] {
        engine.editSong ([] (Song& s) { mergeIdenticalPatterns (s); });
        edited();
    });
    return menu;
}

void PatternPicker::mouseUp (const juce::MouseEvent& e)
{
    // FL's PickerPanelMouseUp FUN_00e4e080: plain = only this one, Ctrl = toggle, Shift = the range from the first picked
    const auto i = rowAt (e.y);
    if (pressedRow < 0 || i != pressedRow)
    {
        pressedRow = -1;
        return;
    }
    pressedRow = -1;
    const auto list = rows();
    if (i >= (int) list.size() || list[(std::size_t) i].kind != ClipKind::pattern)
        return;
    pickersel::leftUp (picked, list[(std::size_t) i].index, flmods::ctrl (e.mods), e.mods.isShiftDown(), deselectOnUp);
    deselectOnUp = false;
    repaint();
}

std::vector<int> PatternPicker::pickedPatterns() const
{
    auto s = picked;
    pickersel::clamp (s, (int) engine.getSong().patterns.size());
    return s.items;
}

void PatternPicker::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto list = rows();
    const auto i = rowAt (e.y);

    if (i >= 0 && list[(std::size_t) i].kind == ClipKind::pattern && onOpenPattern)
        onOpenPattern();
}

void PatternPicker::mouseMove (const juce::MouseEvent& e)
{
    int tab = -1;
    for (int t = 0; t < 3; ++t)
        if (tabBounds (t).contains (e.getPosition()))
            tab = t;
    const auto row = rowAt (e.y);
    const auto add = addBounds().contains (e.getPosition());

    if (tab != hoverTab || row != hoverRow || add != hoverAdd)
    {
        hoverTab = tab;
        hoverRow = row;
        hoverAdd = add;
        repaint();
    }
}

void PatternPicker::mouseExit (const juce::MouseEvent&)
{
    hoverTab = hoverRow = -1;
    hoverAdd = false;
    repaint();
}

ui::Hint PatternPicker::hintAt (juce::Point<int> p)
{
    ui::Hint hint;
    const char* tabs[] = { TR_NOOP ("Picker: patterns"), TR_NOOP ("Picker: audio clips"), TR_NOOP ("Picker: automation clips") };

    for (int t = 0; t < 3; ++t)
        if (tabBounds (t).contains (p))
        {
            hint.text = tr (tabs[t]) + "  " + tr ("[LMB] show");
            return hint;
        }

    if (addBounds().contains (p))
    {
        hint.text = tr ("New pattern");
        return hint;
    }

    const auto list = rows();
    if (const auto i = rowAt (p.y); i >= 0)
    {
        hint.value = list[(std::size_t) i].name;
        hint.text = list[(std::size_t) i].kind == ClipKind::pattern
                        ? tr ("Pattern: [LMB] paint it with the pencil  [Ctrl+LMB] / [Shift+LMB] pick several  [2xLMB] open  [RMB] menu")
                        : tr ("Clip source: [LMB] paint it with the pencil");
        hint.hasMenu = list[(std::size_t) i].kind == ClipKind::pattern;
    }

    return hint;
}

void PatternPicker::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    const auto rows = (int) this->rows().size();
    const auto visible = juce::jmax (1, (listBottom() - headerHeight) / rowHeight);
    scroll = juce::jlimit (0, juce::jmax (0, rows - visible), scroll - (int) std::round (wheel.deltaY * 6.0f));
    repaint();
}
} // namespace lynx
