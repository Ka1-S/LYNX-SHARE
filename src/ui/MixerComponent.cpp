#include "ui/CleanVoiceMenu.h"
#include "ui/FlMods.h"
#include "platform/Paths.h"
#include "ui/ColourPicker.h"
#include "ui/FileChoosers.h"
#include "ui/ImageReadback.h"
#include "ui/MixerComponent.h"
#include "ui/PluginOffTag.h"
#include "ui/SlotWatch.h"
#include "core/ValueMath.h"
#include "ui/UiTrace.h"
#include "ui/TextCache.h"

#include "core/Patcherize.h"
#include "core/Rack.h"
#include "core/FlpImport.h"
#include "engine/FlStudio.h"
#include "core/FlEffects.h"
#include "core/ProjectContents.h"
#include "core/FruityVocoder.h"
#include "core/LiveInput.h"
#include "core/HardwareOutput.h"
#include "core/MixerOps.h"
#include "ui/PluginHeader.h"
#include "core/ProjectSafety.h"
#include "core/Templates.h"
#include "core/RackOps.h"
#include "ui/AutomationUi.h"
#include "ui/BrowserComponent.h"
#include "ui/ExportDialog.h"

#include <cmath>
#include <complex>

#include "core/Biquad.h"
#include "core/Effects.h"
#include "core/GoalKnobs.h"
#include "engine/Text.h"
#include "engine/ProjectFile.h"
#include <numeric>
#include "ui/FacetEqEditor.h"
#include "ui/ParametricEqEditor.h"
#include "ui/PluginEditors.h"
#include "ui/PluginWindow.h"
#include "ui/HintBar.h"
#include "ui/I18n.h"
#include "ui/LiveRemoteUi.h"
#include "core/LynxTune.h"
#include "core/VocalPitch.h"
#include "ui/Icons.h"
#include "ui/ValueControl.h"
#include "ui/Theme.h"
#include "ui/AudioEditor.h"
#include "ui/Dialogs.h"
#include "ui/QuickPick.h"
#include "ui/TrackIcons.h"
#include "core/TrackIcons.h"

namespace lynx
{
namespace
{
// strip layout, top to bottom: header (arm, number, icon, a horizontal name), the vertical name column (not in
// Extra large; low in the strip in the alt layouts), knobs (pan, stereo), mute/solo, meters + fader, the dB read-out,
// then the routing lane (send switch, send knob, wires). The rows follow FL's Mixer layout (MixerStrips::rows()).
constexpr int switchHeight = 16;
constexpr int sendKnobHeight = 26;
constexpr int wiresHeight = 14;
constexpr int laneHeight = switchHeight + sendKnobHeight + wiresHeight + 4;
constexpr int masterScaleWidth = 17; // the dB numbers beside the master meter (round 162, issue #9)

// Fader 0..1 -> dB, with 0.8 = 0 dB (as in FL).
juce::String faderToDb (float volume)
{
    if (volume <= 0.0001f)
        return juce::String::fromUTF8 ("-\xe2\x88\x9e");

    const auto db = juce::Decibels::gainToDecibels (faderGain (volume));
    return (db > 0 ? "+" : "") + juce::String (db, 1);
}

// Meter level 0..1 on a dB scale (-60..+6).
float meterPosition (float gain)
{
    const auto db = juce::Decibels::gainToDecibels (gain, -60.0f);
    return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f);
}

// Stereo separation: -1 = widest, 0 = normal, +1 = mono (MixerTrack::stereo).
ui::ValueSpec stereoSpec (const juce::String& name)
{
    ui::ValueSpec spec;
    spec.name = name;
    spec.minimum = -1.0;
    spec.maximum = 1.0;
    spec.defaultValue = 0.0;
    spec.toText = [] (double v) {
        const auto percent = juce::roundToInt (std::abs (v) * 100.0);
        if (percent == 0)
            return tr ("Normal width");
        if (v > 0.0 && percent >= 100)
            return tr ("Mono");
        return v < 0.0 ? tr ("{0}% wider", { juce::String (percent) }) : tr ("{0}% narrower", { juce::String (percent) });
    };
    spec.fromText = [] (const juce::String& t) -> std::optional<double> {
        const auto lower = t.toLowerCase().trim();
        if (lower.startsWith ("mono"))
            return 1.0;
        if (lower.startsWith ("n") && ! lower.containsAnyOf ("0123456789"))
            return 0.0;
        const auto digits = lower.initialSectionContainingOnly ("+-0123456789.,").replaceCharacter (',', '.');
        if (digits.isEmpty() || digits.containsOnly ("+-."))
            return std::nullopt;
        auto v = digits.getDoubleValue() / 100.0;
        if (lower.contains ("wid") || lower.contains ("larg")) // "50% wider" / "50% mais largo"
            v = -std::abs (v);
        return v;
    };
    return spec;
}

// Online Sessions (round 168a): the amber "R" of an insert whose stand-in render plays
void drawStandInTag (juce::Graphics& g, juce::Rectangle<float> tag)
{
    theme::fillRoundRect (g, tag, 3.0f, theme::colour::warning.withAlpha (0.85f));
    g.setColour (theme::colour::bg1);
    g.setFont (theme::font (9.0f, true));
    g.drawText ("R", tag, juce::Justification::centred, false);
}

ui::ValueSpec sendSpec (const juce::String& name)
{
    // FL's send knob: the fader curve (0 dB at 80 %, +5.6 dB at the top); the value is the knob position
    auto spec = ui::faderSpec (name, unityFader);
    spec.pixelsForRange = 120.0;
    return spec;
}
} // namespace

// The Mixer strips' picture (round 174e2): JUCE's buffered-to-image cache for the visible part of the strips (they are
// as wide as all the inserts), plus the meter levels drawn over it and the meters' own repaints leaving it valid
class MixerStrips::MeterCache final : public juce::CachedComponentImage
{
public:
    explicit MeterCache (MixerStrips& o) : owner (o) {}

    void paint (juce::Graphics& g) override
    {
        const auto scale = g.getInternalContext().getPhysicalPixelScaleFactor();
        auto visible = owner.getLocalBounds();
        if (auto* parent = owner.getParentComponent())
            visible = visible.getIntersection (owner.getLocalArea (parent, parent->getLocalBounds()));
        if (visible.isEmpty())
            return;
        const auto imageSize = (visible.withZeroOrigin().toFloat() * scale).getSmallestIntegerContainer();

        // (round 190bm) a window drawn without the GPU gets a software picture; a picture lost with its graphics
        // device is painted again; no picture at all (no memory): straight to the window
        const auto software = ui::windowDrawnByCpu (owner);
        const auto losses = ui::pictureLossCount();
        if (visible != shown || image.isNull() || image.getBounds() != imageSize || software != softwareImage || losses != lossesSeen)
        {
            if (image.isNull() || image.getBounds() != imageSize || software != softwareImage)
                image = ui::makePicture (imageSize.getWidth(), imageSize.getHeight(), software);
            softwareImage = software;
            lossesSeen = losses;
            shown = visible;
            valid.clear();
        }
        ui::notePicture ("Mixer", image, scale, softwareImage, owner, noted);
        if (image.isNull())
        {
            owner.paintEntireComponent (g, false);
            return;
        }

        if (! valid.containsRectangle (visible))
        {
            const ui::ScopedNoImageReadback noReadback (image); // (the picture is normally only drawn to the window)
            juce::Graphics imageGraphics (image);
            auto& context = imageGraphics.getInternalContext();
            context.addTransform (juce::AffineTransform::translation ((float) -visible.getX(), (float) -visible.getY()).scaled (scale));
            for (const auto& r : valid)
                context.excludeClipRectangle (r);
            context.clipToRectangle (visible);
            ++owner.pictureRefills;
            owner.cachePainting = true;
            owner.paintEntireComponent (imageGraphics, true);
            owner.cachePainting = false;
        }
        valid = visible;

        g.setColour (juce::Colours::black);
        g.drawImageTransformed (image,
                                juce::AffineTransform::scale ((float) visible.getWidth() / (float) imageSize.getWidth(),
                                                              (float) visible.getHeight() / (float) imageSize.getHeight())
                                    .translated ((float) visible.getX(), (float) visible.getY()),
                                false);
        owner.paintMeterLevels (g);
        if (ui::pictureLostMeanwhile (lossesSeen, lossRetries))
            owner.repaint();
    }

    bool invalidateAll() override // (JUCE's repaint() of the whole component comes here, not to invalidate())
    {
        if (! owner.meterRepaintOnly)
        {
            ++owner.pictureInvalidations;
            valid.clear();
        }
        return true;
    }
    bool invalidate (const juce::Rectangle<int>& area) override
    {
        if (! owner.meterRepaintOnly)
        {
            ++owner.pictureInvalidations;
            valid.subtract (area);
        }
        return true;
    }
    void releaseResources() override { image = {}; }

private:
    MixerStrips& owner;
    juce::Image image;
    juce::Rectangle<int> shown; // the visible part the picture holds
    juce::RectangleList<int> valid;
    bool softwareImage = false; // the picture is a software image (the window is drawn without the GPU)
    int lossesSeen = 0;         // ui::pictureLossCount() when the picture was last known good
    int lossRetries = 0;
    juce::String noted; // ui::notePicture's memory
};

MixerStrips::MixerStrips (AudioEngine& e)
    : engine (e)
{
    if (const auto* v = std::getenv ("LYNX_MIXER_CACHE"); v == nullptr || juce::String (v) != "0")
    {
        setCachedComponentImage (new MeterCache (*this)); // (owned by the component)
        meterCacheOn = true;
    }
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
    engine.addChangeListener (this);
    engine.getLevelsBroadcaster().addChangeListener (this);
    updateSize();
    // (the meters and the automated faders move at the display's refresh: `frames`, round 174e)
}

MixerStrips::~MixerStrips()
{
    engine.getLevelsBroadcaster().removeChangeListener (this);
    engine.removeChangeListener (this);
}

void MixerStrips::setVisibleHeight (int height)
{
    visibleHeight = juce::jmax (300, height);
    updateSize();
}

void MixerStrips::updateSize()
{
    const auto tracks = (int) engine.getSong().mixer.size();
    layoutStrips();
    setSize (contentWidth, visibleHeight);
    levels.resize ((std::size_t) juce::jmax (1, tracks) * 2);
}

void MixerStrips::layoutStrips()
{
    const auto& song = engine.getSong();
    const auto tracks = (int) song.mixer.size();
    stripXs.assign ((std::size_t) tracks + 1, 0);
    // "Show only used inserts" (round 163): unused inserts take no room, except those brought back with "+" (so a new
    // insert shows; selecting another strip doesn't move the strips under the mouse)
    hidden.assign ((std::size_t) tracks + 1, false);
    if (showUsedOnly)
    {
        // round 190am: the selected inserts stay, and stay after the selection moves on (no strip jumps under the mouse)
        revealed.insert (selected);
        revealed.insert (selection.begin(), selection.end());
        hidden = mixerops::hiddenUnusedInserts (song, revealed);
    }
    // folded groups (round 177a): nothing is computed unless a group is folded
    foldShow.clear();
    tabOf.clear();
    foldHead.assign ((std::size_t) tracks + 1, false);
    for (int t = 1; t < tracks; ++t)
        if (song.mixer[(std::size_t) t].separator || (t + 1 < tracks && song.mixer[(std::size_t) t + 1].separator))
        {
            // the first strip of a group that can fold carries the chevron (a separator sets groups off)
            const auto group = mixerops::groupOf (song, t);
            if (! group.empty() && group.front() == t && mixerops::canFold (song, t))
                foldHead[(std::size_t) t] = true;
        }
    if (std::any_of (song.mixer.begin() + std::min<std::ptrdiff_t> (1, (std::ptrdiff_t) song.mixer.size()), song.mixer.end(),
                     [] (const MixerTrack& m) { return m.folded; }))
    {
        foldShow = mixerops::foldView (song);
        tabOf.assign ((std::size_t) tracks, -1);
        for (int t = 1; t < tracks; ++t)
        {
            if (foldShow[(std::size_t) t] == mixerops::FoldShow::hidden)
                hidden[(std::size_t) t] = true;
            if (foldShow[(std::size_t) t] == mixerops::FoldShow::tab)
                for (const auto m : mixerops::groupOf (song, t))
                    tabOf[(std::size_t) m] = t;
        }
        for (int t = 1; t < tracks; ++t) // a folded summing group: its bus strip carries the chevron (to unfold it)
            if (foldHead[(std::size_t) t] && hidden[(std::size_t) t] && song.mixer[(std::size_t) t].folded)
                if (const auto bus = mixerops::groupBus (song, mixerops::groupOf (song, t)); bus > 0)
                    foldHead[(std::size_t) bus] = true;
    }
    dockGaps.clear();
    separatorLines.clear();
    vcaXs.clear();
    loudnessX = hiddenX;

    int x = 4;
    bool any = false, afterMaster = false;
    for (auto dock : { MixerDock::left, MixerDock::middle, MixerDock::right })
    {
        const auto d = (int) dock;
        blockStart[d] = x;
        bool first = true;
        for (int t = 0; t < tracks; ++t)
        {
            const auto& track = song.mixer[(std::size_t) t];
            if (track.dock != dock)
                continue;
            if (hidden[(std::size_t) t])
            {
                stripXs[(std::size_t) t] = hiddenX;
                continue;
            }
            if (any && first && dock == MixerDock::right)
            {
                // the gap between two blocks belongs to the pinned block: the right block's is before its strips,
                // the left block's after them (below)
                dockGaps.push_back ({ x + dockGap / 2 - 2, dock });
                x += dockGap;
            }
            else if (! first && track.separator && t > 0)
            {
                separatorLines.push_back ({ x + separatorGap / 2 - 3, dock });
                x += separatorGap;
            }
            else if (afterMaster)
                x += 8;
            if (t == 0 && ! song.vcas.empty()) // the VCA strips: their own block left of the Master (round 177a)
            {
                for (std::size_t i = 0; i < song.vcas.size(); ++i)
                {
                    vcaXs.push_back (x);
                    x += spec().stripWidth;
                }
                x += 8;
            }
            stripXs[(std::size_t) t] = x;
            x += stripWidthOf (t);
            if (t == 0 && showLoudness) // the loudness panel: right of the Master, in its dock (round 190am)
            {
                loudnessX = x;
                x += loudnessWidth;
            }
            any = true;
            first = false;
            afterMaster = t == 0;
        }
        if (dock == MixerDock::middle) // the "+" button ends the middle block
        {
            if (afterMaster)
                x += 8;
            stripXs[(std::size_t) tracks] = x;
            x += spec().stripWidth;
            afterMaster = false;
            any = true;
        }
        blockEnd[d] = x;
        if (dock == MixerDock::left && ! first) // the left block carries its gap to the middle
        {
            dockGaps.push_back ({ x + dockGap / 2 - 2, dock });
            x += dockGap;
            blockEnd[d] = x;
            afterMaster = false;
        }
    }
    contentWidth = x + 8;
}

MixerDock MixerStrips::dockOf (int track) const noexcept
{
    const auto& mixer = engine.getSong().mixer;
    return track >= 0 && track < (int) mixer.size() ? mixer[(std::size_t) track].dock : MixerDock::middle;
}

int MixerStrips::pinOffset (MixerDock dock) const noexcept
{
    auto* parent = getParentComponent();
    if (parent == nullptr || dock == MixerDock::middle)
        return 0;
    const auto scroll = juce::jmax (0, -getX());
    const auto visible = parent->getWidth();
    if (dock == MixerDock::left)
        return blockEnd[0] > blockStart[0] ? juce::jmin (scroll, juce::jmax (0, contentWidth - blockEnd[0])) : 0;
    return blockEnd[2] > blockStart[2] ? juce::jmin (0, scroll + visible - contentWidth) : 0;
}

int MixerStrips::stripX (int track) const noexcept
{
    if (track <= -2) // a VCA strip (vcaStrip): it follows the Master's dock
    {
        const auto i = (std::size_t) (-2 - track);
        return i < vcaXs.size() ? vcaXs[i] + pinOffset (dockOf (0)) : hiddenX;
    }
    if (track >= 0 && track < (int) stripXs.size())
        return stripXs[(std::size_t) track] + pinOffset (dockOf (track));
    return track == 0 ? 4 : 4 + spec().masterWidth + 8 + (track - 1) * spec().stripWidth;
}

bool MixerStrips::isInterestedInDragSource (const SourceDetails& details)
{
    if (BrowserComponent::isEffectDrag (details) || BrowserComponent::isGeneratorDrag (details))
        return true;
    if (! BrowserComponent::isBrowserDrag (details))
        return false;
    // (asked on every mouse move of the drag: the answer for the dragged file is kept)
    static juce::File lastFile;
    static bool lastAnswer = false;
    if (const auto file = BrowserComponent::draggedFile (details); file != lastFile)
    {
        lastFile = file;
        lastAnswer = mixerpresets::isMixerPreset (file);
    }
    return lastAnswer;
}

void MixerStrips::itemDropped (const SourceDetails& details)
{
    const auto t = trackAt (details.localPosition.x);
    const auto tracks = (int) engine.getSong().mixer.size();
    // FL's mixer preset dropped from the Browser (WhatsNew): onto a track it loads there, onto the empty mixer space
    // (or "+") it makes a new track
    if (BrowserComponent::isBrowserDrag (details))
    {
        const auto file = BrowserComponent::draggedFile (details);
        if (! mixerpresets::isMixerPreset (file))
            return;
        if (t >= 0 && t < tracks)
            mixerpresets::loadOnTracks (engine, { t }, file);
        else if (const auto added = mixerpresets::insertTrack (engine, tracks, file); added > 0)
        {
            revealed.insert (added);
            updateSize();
            selectTrack (added);
        }
        return;
    }
    if (t < 0 || t >= tracks)
        return;
    if (auto fx = BrowserComponent::draggedEffect (details); fx && onEffectDropped != nullptr)
        onEffectDropped (t, std::move (*fx));
    else if (auto c = BrowserComponent::draggedGenerator (details); c && onInstrumentDropped != nullptr)
        onInstrumentDropped (t, std::move (*c));
}

int MixerStrips::trackAt (int x) const noexcept
{
    const auto tracks = (int) engine.getSong().mixer.size();

    // the pinned blocks lie over the scrolled middle: they are hit first
    for (auto dock : { MixerDock::left, MixerDock::right, MixerDock::middle })
        for (int t = 0; t <= tracks; ++t) // t == tracks = "+" button
            if (dockOf (t) == dock && x >= stripX (t) && x < stripX (t) + stripWidthOf (t))
                return t;

    return -1;
}

int MixerStrips::laneTop() const noexcept
{
    return getHeight() - laneHeight;
}

const mixerlayout::Spec& MixerStrips::spec() noexcept
{
    static mixerlayout::Layout cachedFor = (mixerlayout::Layout) -1;
    static mixerlayout::Spec cached;
    if (cachedFor != layout)
    {
        cached = mixerlayout::specOf (layout);
        cachedFor = layout;
    }
    return cached;
}

void MixerStrips::setLayout (mixerlayout::Layout newLayout)
{
    if (newLayout == layout)
        return;
    layout = newLayout;
    updateSize();
    repaint();
    if (onLayoutChanged)
        onLayoutChanged();
}

MixerStrips::Rows MixerStrips::rows() const noexcept
{
    const auto& sp = spec();
    const auto compact = mixerlayout::isCompact (layout);
    Rows r;
    r.header = sp.verticalName ? 38 : 54;
    // the vertical name column takes what the fader can spare (FL's runs the strip's upper part)
    r.nameHeight = sp.verticalName ? juce::jlimit (40, 110, (getHeight() - 300) / 2) : 0;
    r.knobsHeight = ! sp.stereoKnob ? sp.knobSize + 8 : sp.stackedKnobs ? sp.knobSize + (sp.knobSize - 6) + 10 : 32;
    r.buttonsHeight = compact ? 32 : 20; // compact: mute above solo
    r.readoutHeight = sp.dbReadout ? 16 : 0;

    auto y = r.header;
    if (sp.verticalName && ! sp.nameLow)
    {
        r.nameTop = y;
        y += r.nameHeight;
    }
    r.knobsTop = y;
    r.buttonsTop = r.knobsTop + r.knobsHeight;
    r.faderTop = r.buttonsTop + r.buttonsHeight + 6;

    auto bottom = laneTop() - r.readoutHeight - 4;
    if (compact)
    {
        r.armY = bottom - 14;
        bottom -= 16;
    }
    if (sp.verticalName && sp.nameLow)
    {
        // the alt layouts: the name runs up from the strip's bottom; the dB read-out stays under the fader, above the
        // name (rel 0.1.23: the read-out was drawn over the name's top)
        r.nameTop = bottom + r.readoutHeight - r.nameHeight;
        bottom = r.nameTop - 4 - r.readoutHeight;
    }
    r.faderBottom = juce::jmax (r.faderTop + 40, bottom);
    return r;
}

juce::Rectangle<int> MixerStrips::foldArea (int t) const
{
    // the chevron left of the number on a group's first strip (round 177a)
    return { stripX (t) + 1, 3, 12, 15 };
}

float MixerStrips::tabLevel (int t) const noexcept
{
    float level = 0.0f;
    for (std::size_t m = 0; m < tabOf.size(); ++m)
        if (tabOf[m] == t)
            for (std::size_t ch = 0; ch < 2; ++ch)
                if (const auto i = m * 2 + ch; i < levels.size())
                    level = juce::jmax (level, levels[i].level);
    return level;
}

void MixerStrips::paintFoldTab (juce::Graphics& g, int t)
{
    // a folded group without a bus (round 177a): a slim tab in the group's colour with its name, how many inserts it
    // holds and the loudest member's level; a click unfolds it. The members are never hidden without a trace.
    using namespace theme;
    const auto& song = engine.getSong();
    const auto& head = song.mixer[(std::size_t) t];
    const auto x = stripX (t);
    const auto area = juce::Rectangle<int> (x, 2, foldTabWidth - 2, laneTop() - 2).toFloat();
    const auto hoveredTab = hoverTrack == t;
    auto plate = hoveredTab ? colour::bg3 : colour::bg2;
    if (head.colour != 0)
        plate = plate.interpolatedWith (tame (juce::Colour (head.colour)), 0.3f);
    theme::fillRoundRect (g, area, radius::md, plate);
    if (head.colour != 0)
        theme::fillRoundRect (g, area.withHeight (3.0f).reduced (3.0f, 0.0f), 1.5f, tame (juce::Colour (head.colour)));
    g.setColour (hoveredTab ? colour::borderStrong : colour::border);
    g.drawRoundedRectangle (area.reduced (0.5f), radius::md, 1.0f);
    icons::draw (g, Icon::chevronDown, juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ area.getCentreX(), 12.0f }).transformedBy (
                                           juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, area.getCentreX(), 12.0f)),
                 hoveredTab ? colour::textPrimary : colour::textSecondary);

    const auto members = (int) mixerops::groupOf (song, t).size();
    g.setColour (colour::textSecondary);
    g.setFont (font (9.0f, true));
    textcache::drawText (g, juce::String (members), (int) area.getX(), 22, (int) area.getWidth(), 12, juce::Justification::centred);

    // the loudest member's level: one thin bar along the fader rows
    const auto rail = faderRail (t);
    const auto bar = juce::Rectangle<float> (area.getRight() - 6.0f, (float) rail.getY(), 3.0f, (float) rail.getHeight());
    theme::fillRoundRect (g, bar, 1.0f, colour::bg0);
    if (! cachePainting) // (cached: drawn over the picture, paintMeterLevels)
        paintFoldTabLevel (g, t);

    // the group's name, bottom to top
    const auto name = juce::String::fromUTF8 (head.name.c_str());
    const auto column = juce::Rectangle<int> ((int) area.getX() + 1, 40, (int) area.getWidth() - 8, juce::jmax (20, rail.getY() - 46));
    juce::Graphics::ScopedSaveState state (g);
    g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, (float) column.getCentreX(), (float) column.getCentreY()));
    g.setColour (colour::textPrimary.withAlpha (0.9f));
    g.setFont (font (type::small, false));
    textcache::drawFittedText (g, name, juce::Rectangle<int> (column.getHeight(), column.getWidth()).withCentre (column.getCentre()),
                               juce::Justification::centredLeft, 1, 0.8f);
}

void MixerStrips::paintFoldTabLevel (juce::Graphics& g, int t) const
{
    using namespace theme;
    const auto rail = faderRail (t);
    const auto bar = juce::Rectangle<float> ((float) (stripX (t) + foldTabWidth - 2) - 6.0f, (float) rail.getY(), 3.0f, (float) rail.getHeight());
    const auto level = tabLevel (t);
    g.setColour (level >= 1.0f ? colour::meterClip : level >= 0.5f ? colour::meterMid : colour::meterLow);
    g.fillRect (bar.withTop (bar.getBottom() - meterPosition (level) * bar.getHeight()));
}

// --- VCA strips (round 177a slice 3; REA_rodada177a §3) ---------------------------------------------------------------
// A VCA strip carries no audio: its fader scales its members' faders, its mute mutes them, its solo solos them. It has
// no slots, pan, EQ or routing; a dashed outline and the "VCA" label set it apart from an insert.

int MixerStrips::vcaAt (int x) const noexcept
{
    for (int i = 0; i < (int) vcaXs.size(); ++i)
        if (const auto left = stripX (vcaStrip (i)); x >= left && x < left + spec().stripWidth)
            return i;
    return -1;
}

MixerStrips::Part MixerStrips::vcaPartAt (int i, juce::Point<int> p) const
{
    const auto v = vcaStrip (i);
    if (headerArea (v).contains (p) || nameArea (v).contains (p))
        return Part::header;
    if (muteArea (v).contains (p))
        return Part::mute;
    if (soloArea (v).contains (p))
        return Part::solo;
    if (meterArea (v).contains (p))
        return Part::meter;
    if (faderRail (v).expanded (0, 6).contains (p))
        return Part::fader;
    return Part::body;
}

float MixerStrips::vcaMeter (int i, int channel) const noexcept
{
    const auto& song = engine.getSong();
    float level = 0.0f;
    if (i < 0 || i >= (int) song.vcas.size())
        return level;
    for (const auto m : mixerops::vcaReach (song, i))
        if (const auto index = (std::size_t) m * 2 + (std::size_t) channel; index < levels.size())
            level = juce::jmax (level, levels[index].level);
    return level;
}

float MixerStrips::vcaShownLevel (int i) const noexcept
{
    const auto& vcas = engine.getSong().vcas;
    if (i < 0 || i >= (int) vcas.size())
        return unityFader;
    return engine.getAutomationDisplay().shown (AutomationTarget { AutomationTargetKind::vcaLevel, i }, vcas[(std::size_t) i].level);
}

ui::ValueSpec MixerStrips::vcaSpec (int i) const
{
    const auto& vcas = engine.getSong().vcas;
    const auto name = i >= 0 && i < (int) vcas.size() ? juce::String::fromUTF8 (vcas[(std::size_t) i].name.c_str()) : juce::String();
    auto spec = ui::faderSpec (name + " - " + tr ("VCA level"), unityFader);
    spec.pixelsForRange = juce::jmax (60, faderRail (vcaStrip (i)).getHeight());
    spec.target = AutomationTarget { AutomationTargetKind::vcaLevel, i };
    return spec;
}

void MixerStrips::setVcaLevel (int i, double value)
{
    if (i < 0 || i >= (int) engine.getSong().vcas.size())
        return;
    // like an insert's fader: only the live levels move during a drag (the gain path reads them lock-free)
    engine.editLevels ([i, v = juce::jlimit (0.0f, 1.0f, (float) value)] (Song& s) {
        if (i < (int) s.vcas.size())
            s.vcas[(std::size_t) i].level = v;
    });
}

void MixerStrips::repaintVca (int i)
{
    if (i < 0 || i >= (int) vcaXs.size())
        return;
    repaint (stripX (vcaStrip (i)) - 2, 0, spec().stripWidth + 4, laneTop());
    // the members' ticks (the level that plays) move with it
    for (const auto m : mixerops::vcaReach (engine.getSong(), i))
        if (m > 0 && m < (int) stripXs.size() && ! isHidden (m))
        {
            const auto fader = faderArea (m);
            repaint (stripX (m), fader.getY() - 9, stripWidthOf (m), fader.getHeight() + 18);
        }
}

void MixerStrips::soloVca (int i)
{
    const auto reach = mixerops::vcaReach (engine.getSong(), i);
    if (! reach.empty())
        engine.editSong ([reach] (Song& s) { mixerops::soloSelected (s, reach, false); });
}

juce::String MixerStrips::vcaNote (int track) const
{
    const auto& song = engine.getSong();
    if (song.vcas.empty() || track <= 0 || track >= (int) song.mixer.size())
        return {};
    const auto chain = mixerops::vcasOf (song, track);
    if (chain.empty())
        return {};
    juce::StringArray parts;
    for (const auto v : chain)
    {
        const auto& vca = song.vcas[(std::size_t) v];
        parts.add ("VCA '" + juce::String::fromUTF8 (vca.name.c_str()) + "' " + faderToDb (vca.level) + " dB" + (vca.muted ? " (" + tr ("Muted") + ")" : juce::String()));
    }
    const auto playing = faderPosition (faderGain (song.mixer[(std::size_t) track].volume) * mixerops::vcaGain (song, track));
    return "(" + parts.joinIntoString (", ") + juce::String::fromUTF8 (" \xe2\x86\x92 ") + faderToDb (playing) + " dB)";
}

ui::Hint MixerStrips::vcaHint (int i, juce::Point<int> p) const
{
    ui::Hint h;
    const auto& song = engine.getSong();
    if (i < 0 || i >= (int) song.vcas.size())
        return h;
    const auto& vca = song.vcas[(std::size_t) i];
    const auto name = juce::String::fromUTF8 (vca.name.c_str());
    const auto part = dragVca >= 0 ? Part::fader : vcaPartAt (i, p);
    switch (part)
    {
        case Part::fader:
            return ui::valueHint (vcaSpec (i), vcaShownLevel (i), ui::valueGestures());
        case Part::mute: h.text = name + " - " + tr ("Click: mute the VCA's members.  Right-click or Ctrl+click: solo"); break;
        case Part::solo: h.text = name + " - " + tr ("Solo the VCA's members"); break;
        case Part::meter: h.text = name + " - " + tr ("The loudest member's level"); break;
        default:
            h.text = name + " - " + tr ("VCA fader: moves the level of its {0} inserts (they keep their own faders)  [LMB] selects them  [RMB] VCA menu",
                                        { juce::String ((int) vca.members.size()) });
            h.hasMenu = true;
            break;
    }
    return h;
}

void MixerStrips::vcaMouseDown (const juce::MouseEvent& e, int i)
{
    const auto& song = engine.getSong();
    if (i >= (int) song.vcas.size())
        return;
    const auto v = vcaStrip (i);
    const auto part = vcaPartAt (i, e.getPosition());

    if (part == Part::mute && (flmods::popup (e.mods) || flmods::ctrl (e.mods)))
    {
        soloVca (i);
        return;
    }
    if (flmods::popup (e.mods))
    {
        showVcaMenu (i);
        return;
    }
    switch (part)
    {
        case Part::mute:
            engine.editSong ([i] (Song& s) {
                if (i < (int) s.vcas.size())
                    s.vcas[(std::size_t) i].muted = ! s.vcas[(std::size_t) i].muted;
            });
            return;
        case Part::solo:
            soloVca (i);
            return;
        case Part::fader:
        {
            if (ui::isResetClick (e))
            {
                setVcaLevel (i, unityFader);
                return;
            }
            const auto start = song.vcas[(std::size_t) i].level;
            if (! flmods::ctrl (e.mods)) // a press on the rail steps the fader towards the mouse (as an insert's)
            {
                const auto rail = faderRail (v);
                if (std::abs ((float) e.y - ((float) rail.getBottom() - start * (float) rail.getHeight())) > 9.0f)
                {
                    pressRail (v, e.y);
                    return;
                }
            }
            dragVca = i;
            drag.begin (e, start);
            ui::showValueTip (*this, faderRail (v), vcaSpec (i).text (start));
            return;
        }
        default:
        {
            // the header or the body selects the members (so the insert menu's VCA items act on them)
            std::vector<int> members;
            for (const auto m : song.vcas[(std::size_t) i].members)
                if (m > 0 && m < (int) song.mixer.size())
                    members.push_back (m);
            if (! members.empty())
                setSelection (members, members.front());
            return;
        }
    }
}

void MixerStrips::showVcaMenu (int i)
{
    const auto& song = engine.getSong();
    if (i < 0 || i >= (int) song.vcas.size())
        return;
    const auto& vca = song.vcas[(std::size_t) i];
    juce::PopupMenu menu;
    auto rename = [this, i] {
        if (i >= (int) engine.getSong().vcas.size())
            return;
        const auto& item = engine.getSong().vcas[(std::size_t) i];
        showNameAndColourDialog (tr ("Rename VCA"), juce::String::fromUTF8 (item.name.c_str()), item.colour,
                                 [this, i] (const juce::String& name, std::uint32_t colour) {
                                     engine.editSong ([i, n = name.trim().toStdString(), colour] (Song& s) {
                                         if (i >= (int) s.vcas.size())
                                             return;
                                         if (! n.empty())
                                             s.vcas[(std::size_t) i].name = n;
                                         s.vcas[(std::size_t) i].colour = colour;
                                     });
                                 });
    };
    ui::addItemHeader (menu, tr ("VCA"), juce::String::fromUTF8 (vca.name.c_str()), vca.colour != 0 ? juce::Colour (vca.colour) : juce::Colour(), rename);
    menu.addItem (tr ("Rename and color..."), rename);

    // the selected inserts (not the Master) join or leave this VCA
    std::vector<int> picked;
    for (const auto m : selection)
        if (m > 0 && m < (int) song.mixer.size())
            picked.push_back (m);
    const auto in = [&vca] (int m) { return std::binary_search (vca.members.begin(), vca.members.end(), m); };
    menu.addItem (tr ("Assign selected inserts"), ! picked.empty() && ! std::all_of (picked.begin(), picked.end(), in), false, [this, picked, i] {
        engine.editSong ([picked, i] (Song& s) { mixerops::assignToVca (s, picked, i); });
    });
    menu.addItem (tr ("Remove selected inserts"), std::any_of (picked.begin(), picked.end(), in), false, [this, picked, i] {
        engine.editSong ([picked, i] (Song& s) { mixerops::removeFromVca (s, picked, i); });
    });

    // nesting: this VCA follows another one (never one that already follows it)
    juce::PopupMenu follow;
    follow.addItem (tr ("None"), true, vca.parent < 0, [this, i] {
        engine.editSong ([i] (Song& s) {
            if (i < (int) s.vcas.size())
                s.vcas[(std::size_t) i].parent = -1;
        });
    });
    for (int other = 0; other < (int) song.vcas.size(); ++other)
    {
        if (other == i)
            continue;
        bool loops = false; // `other` follows `i` (directly or through others)
        for (int up = other, guard = 0; up >= 0 && up < (int) song.vcas.size() && guard <= (int) song.vcas.size(); up = song.vcas[(std::size_t) up].parent, ++guard)
            if (up == i)
            {
                loops = true;
                break;
            }
        follow.addItem (juce::String::fromUTF8 (song.vcas[(std::size_t) other].name.c_str()), ! loops, vca.parent == other, [this, i, other] {
            engine.editSong ([i, other] (Song& s) {
                if (i < (int) s.vcas.size() && other < (int) s.vcas.size())
                    s.vcas[(std::size_t) i].parent = other;
            });
        });
    }
    menu.addSubMenu (tr ("Follow VCA"), follow, song.vcas.size() > 1);
    menu.addSeparator();
    menu.addItem (tr ("Merge VCA into members"), ! vca.members.empty(), false, [this, i] {
        engine.editSong ([i] (Song& s) { mixerops::mergeVcaIntoMembers (s, i); });
    });
    menu.addItem (tr ("Delete VCA"), [this, i] {
        engine.editSong ([i] (Song& s) { mixerops::deleteVca (s, i); });
        updateSize();
    });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

void MixerStrips::paintVcaMeter (juce::Graphics& g, int i) const
{
    using namespace theme;
    const auto& sp = spec();
    const auto meters = meterArea (vcaStrip (i));
    for (int ch = 0; ch < 2; ++ch)
    {
        const auto bar = juce::Rectangle<int> (meters.getX() + ch * (sp.meterWidth + 2), meters.getY(), sp.meterWidth, meters.getHeight());
        const auto level = vcaMeter (i, ch);
        const auto h = (int) (meterPosition (level) * (float) bar.getHeight());
        g.setColour ((level >= 1.0f ? colour::meterClip : level >= 0.5f ? colour::meterMid : colour::meterLow).withAlpha (0.8f));
        g.fillRect (bar.withTop (bar.getBottom() - h));
    }
}

void MixerStrips::paintVcaStrip (juce::Graphics& g, int i)
{
    using namespace theme;
    const auto& song = engine.getSong();
    if (i < 0 || i >= (int) song.vcas.size())
        return;
    const auto& vca = song.vcas[(std::size_t) i];
    const auto v = vcaStrip (i);
    const auto x = stripX (v);
    const auto& sp = spec();
    const auto w = sp.stripWidth;
    if (! g.clipRegionIntersects ({ x, 0, w, laneTop() }))
        return;
    const auto plateWidth = w - sp.gap;
    const auto compactLayout = mixerlayout::isCompact (layout);
    const auto strip = juce::Rectangle<int> (x, 2, plateWidth, laneTop() - 2).toFloat();
    const auto hovered = [this, v] (Part p) { return hoverTrack == v && hoverPart == p; };
    const auto colourOf = vca.colour != 0 ? tame (juce::Colour (vca.colour)) : colour::info;

    auto plate = hoverTrack == v ? colour::bg3 : colour::bg2;
    plate = plate.interpolatedWith (colourOf, 0.12f);
    theme::fillRoundRect (g, strip, radius::md, plate);
    theme::fillRoundRect (g, strip.withHeight (3.0f).reduced (6.0f, 0.0f), 1.5f, colourOf);
    {
        // a dashed outline: this strip carries no audio
        juce::Path outline;
        outline.addRoundedRectangle (strip.reduced (0.5f), radius::md);
        juce::Path dashed;
        const float dashes[] = { 3.0f, 3.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dashes, 2);
        g.setColour (colourOf.withAlpha (0.55f));
        g.fillPath (dashed);
    }

    // header: "VCA" where an insert shows its number, the member count under it, the name as on an insert
    g.setColour (colour::textSecondary);
    g.setFont (font (compactLayout ? 9.5f : type::caption, true));
    textcache::drawFittedText (g, "VCA", juce::Rectangle<int> (x, 4, plateWidth, 13), juce::Justification::centred, 1, 0.6f);
    g.setFont (font (9.0f));
    textcache::drawFittedText (g, juce::String::fromUTF8 ("\xc3\x97") + juce::String ((int) vca.members.size()), iconArea (v),
                               juce::Justification::centred, 1, 0.6f);

    const auto name = juce::String::fromUTF8 (vca.name.c_str());
    g.setColour (vca.muted ? colour::textDisabled : colour::textPrimary.withAlpha (0.9f));
    g.setFont (font (compactLayout ? 10.0f : type::small));
    if (const auto box = nameArea (v); ! sp.verticalName)
        textcache::drawFittedText (g, name, box, juce::Justification::centred, 1, 0.7f);
    else
    {
        const auto column = box.reduced (2, 4);
        juce::Graphics::ScopedSaveState state (g);
        g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, (float) column.getCentreX(), (float) column.getCentreY()));
        const auto rotated = juce::Rectangle<int> (column.getHeight(), column.getWidth()).withCentre (column.getCentre());
        textcache::drawFittedText (g, name, rotated, juce::Justification::centredLeft, 1, 0.8f);
    }

    // mute (the members) and solo (the members)
    {
        const auto led = compactLayout ? 10.0f : 12.0f;
        const auto m = muteArea (v).toFloat().withSizeKeepingCentre (led, led);
        g.setColour (muteLed (vca.muted));
        g.fillEllipse (m);
        if (! vca.muted || hovered (Part::mute))
        {
            g.setColour (hovered (Part::mute) ? colour::textSecondary : colour::ledOn.withAlpha (0.25f));
            g.drawEllipse (m.expanded (2.0f), 1.0f);
        }
        const auto reach = mixerops::vcaReach (song, i);
        const auto soloed = ! reach.empty() && std::all_of (reach.begin(), reach.end(), [&song] (int t) {
            return t > 0 && t < (int) song.mixer.size() && song.mixer[(std::size_t) t].solo;
        });
        const auto s = soloArea (v).toFloat().reduced (2.0f);
        theme::fillRoundRect (g, s, radius::sm, soloed ? colour::accent : hovered (Part::solo) ? colour::bg5 : colour::bg0);
        g.setColour (soloed ? colour::onAccent : hovered (Part::solo) ? colour::textPrimary : colour::textSecondary);
        g.setFont (font (type::caption, true));
        textcache::drawText (g, "S", s.toNearestInt(), juce::Justification::centred);
    }

    // the loudest member's meter and the VCA's fader
    const auto area = faderArea (v);
    const auto meters = meterArea (v);
    const auto fader = faderRail (v);
    for (int ch = 0; ch < 2; ++ch)
    {
        const auto bar = juce::Rectangle<int> (meters.getX() + ch * (sp.meterWidth + 2), meters.getY(), sp.meterWidth, meters.getHeight());
        theme::fillRoundRect (g, bar.toFloat(), 1.5f, colour::bg0);
    }
    if (! cachePainting) // (cached: drawn over the picture, paintMeterLevels)
        paintVcaMeter (g, i);
    const auto railX = (float) fader.getCentreX();
    const auto level = vcaShownLevel (i);
    const auto capY = (float) fader.getBottom() - level * (float) fader.getHeight();
    theme::fillRoundRect (g, { railX - 2.0f, (float) fader.getY(), 4.0f, (float) fader.getHeight() }, 2.0f, colour::bg0);
    theme::fillRoundRect (g, { railX - 2.0f, capY, 4.0f, juce::jmax (0.0f, (float) fader.getBottom() - capY) }, 2.0f, colourOf.withAlpha (vca.muted ? 0.35f : 0.8f));
    const auto unityY = (float) fader.getBottom() - unityFader * (float) fader.getHeight();
    g.setColour (colour::textDisabled);
    g.drawHorizontalLine ((int) unityY, (float) fader.getX(), railX - 5.0f);
    g.drawHorizontalLine ((int) unityY, railX + 5.0f, (float) fader.getRight());
    const auto capActive = hovered (Part::fader) || dragVca == i;
    const auto capWidth = juce::jmin ((float) fader.getWidth() + 2.0f, 26.0f);
    const auto cap = juce::Rectangle<float> (capWidth, 12.0f).withCentre ({ railX, capY });
    theme::fillRoundRect (g, cap.translated (0.0f, 1.5f), radius::sm, juce::Colours::black.withAlpha (0.35f));
    theme::fillRoundRect (g, cap, radius::sm, capActive ? juce::Colour (0xffffffff) : juce::Colour (0xffd4d7dd));
    g.setColour (colourOf);
    g.fillRect (cap.withSizeKeepingCentre (capWidth - 8.0f, 2.0f));
    if (sp.dbReadout)
    {
        g.setColour (colour::textSecondary);
        g.setFont (font (type::caption));
        textcache::drawFittedText (g, faderToDb (level) + " dB", juce::Rectangle<int> (x, area.getBottom() + 3, plateWidth, 14), juce::Justification::centred, 1, 0.7f);
    }
}

juce::Rectangle<int> MixerStrips::headerArea (int t) const
{
    return { stripX (t), 0, stripWidthOf (t) - spec().gap, rows().header };
}

juce::Rectangle<int> MixerStrips::nameArea (int t) const
{
    const auto r = rows();
    const auto w = stripWidthOf (t) - spec().gap;
    if (r.nameHeight <= 0) // Extra large: the name row of the header
        return { stripX (t) + 3, 38, w - 6, 14 };
    // the name runs up the strip's left part; the right part shows the meters (as FL's)
    const auto nameWidth = mixerlayout::isCompact (layout) ? w : juce::jmax (14, w - 2 * spec().meterWidth - 8);
    return { stripX (t), r.nameTop, nameWidth, r.nameHeight };
}

juce::Rectangle<int> MixerStrips::iconArea (int t) const
{
    const auto w = stripWidthOf (t) - spec().gap;
    const auto size = juce::jmin (16, w - 2);
    return { stripX (t) + (w - size) / 2, rows().iconY, size, size };
}

juce::Rectangle<int> MixerStrips::armArea (int t) const
{
    const auto r = rows();
    if (r.armY >= 0)
    {
        const auto w = stripWidthOf (t) - spec().gap;
        return { stripX (t) + (w - 13) / 2, r.armY, 13, 13 };
    }
    return { stripX (t) + 4, 4, 13, 13 };
}

juce::Rectangle<int> MixerStrips::inputArea (int t) const
{
    // a thin level bar under the header (inserts with an input source)
    return { stripX (t) + 3, rows().header - 3, stripWidthOf (t) - spec().gap - 6, 3 };
}

juce::Rectangle<int> MixerStrips::panArea (int t) const
{
    const auto r = rows();
    const auto& sp = spec();
    const auto w = stripWidthOf (t) - sp.gap;
    if (! sp.stereoKnob)
        return { stripX (t), r.knobsTop, w, r.knobsHeight };
    if (sp.stackedKnobs)
        return { stripX (t), r.knobsTop, w, sp.knobSize + 4 };
    return { stripX (t), r.knobsTop, w / 2, r.knobsHeight };
}

juce::Rectangle<int> MixerStrips::stereoArea (int t) const
{
    const auto r = rows();
    const auto& sp = spec();
    const auto w = stripWidthOf (t) - sp.gap;
    if (! sp.stereoKnob)
        return {};
    if (sp.stackedKnobs)
        return { stripX (t), r.knobsTop + sp.knobSize + 4, w, r.knobsHeight - sp.knobSize - 4 };
    return { stripX (t) + w / 2, r.knobsTop, w - w / 2, r.knobsHeight };
}

juce::Rectangle<int> MixerStrips::muteArea (int t) const
{
    const auto r = rows();
    const auto w = stripWidthOf (t) - spec().gap;
    if (mixerlayout::isCompact (layout))
        return { stripX (t), r.buttonsTop, w, 16 };
    if (t == 0)
        return { stripX (t) + 4, r.buttonsTop, 24, r.buttonsHeight };
    return { stripX (t) + 2, r.buttonsTop, w / 2 - 2, r.buttonsHeight };
}

juce::Rectangle<int> MixerStrips::soloArea (int t) const
{
    const auto r = rows();
    const auto w = stripWidthOf (t) - spec().gap;
    if (mixerlayout::isCompact (layout))
        return { stripX (t) + (w - 16) / 2, r.buttonsTop + 16, 16, 16 };
    return { stripX (t) + w - juce::jmin (24, w / 2), r.buttonsTop, juce::jmin (24, w / 2), r.buttonsHeight };
}

juce::Rectangle<int> MixerStrips::faderArea (int t) const
{
    const auto r = rows();
    const auto inset = mixerlayout::isCompact (layout) ? 1 : 5;
    return { stripX (t) + inset, r.faderTop, stripWidthOf (t) - spec().gap - 2 * inset, r.faderBottom - r.faderTop };
}

juce::Rectangle<int> MixerStrips::meterArea (int t) const
{
    return faderArea (t).withWidth (spec().meterWidth * 2 + 2);
}

juce::Rectangle<int> MixerStrips::grArea (int t) const
{
    // in the gap between the meters and the fader (compact layouts have none: over the right meter's edge)
    const auto meters = meterArea (t);
    return mixerlayout::isCompact (layout) ? meters.withTrimmedLeft (meters.getWidth() - 2) : meters.withX (meters.getRight() + 1).withWidth (3);
}

float MixerStrips::gainReduction (int t) const
{
    const auto& mixer = engine.getSong().mixer;
    if (t <= 0 || t >= (int) mixer.size())
        return -1.0f;
    const auto* times = engine.getProcessTimes();
    auto gr = -1.0f;
    const auto& slots = mixer[(std::size_t) t].effects;
    for (int s = 0; s < (int) slots.size() && s < maxEffectSlots; ++s)
    {
        if (! slots[(std::size_t) s].enabled || ! mixerops::reportsGainReduction (slots[(std::size_t) s].kind))
            continue;
        const auto key = (std::size_t) (t * maxEffectSlots + s) * ProcessTimes::meterOutputs;
        const auto v = times != nullptr && key < times->effectMeters.size() ? times->effectMeters[key].load (std::memory_order_relaxed) : 0.0f;
        gr = juce::jmax (gr, juce::jlimit (0.0f, 1.0f, v));
    }
    return gr;
}

juce::Rectangle<int> MixerStrips::faderRail (int t) const
{
    return faderArea (t).withTrimmedLeft (spec().meterWidth * 2 + (mixerlayout::isCompact (layout) ? 3 : 6) + (t == 0 ? masterScaleWidth : 0));
}

juce::Rectangle<int> MixerStrips::routeArea (int t) const
{
    const auto inset = mixerlayout::isCompact (layout) ? 1 : 5;
    return { stripX (t) + inset, laneTop() + 2, stripWidthOf (t) - spec().gap - 2 * inset, switchHeight };
}

juce::Rectangle<int> MixerStrips::sendArea (int t) const
{
    return { stripX (t), laneTop() + 2 + switchHeight, stripWidthOf (t) - spec().gap, sendKnobHeight };
}

MixerStrips::Part MixerStrips::partAt (int t, juce::Point<int> p) const
{
    const auto& song = engine.getSong();

    if (t == (int) song.mixer.size())
        return Part::add;

    if (t < 0 || t > (int) song.mixer.size())
        return Part::none;

    const auto sending = selected > 0 && t != selected && selected < (int) song.mixer.size();

    if (isFoldTab (t))
        return Part::tab;
    if (t > 0 && t < (int) foldHead.size() && foldHead[(std::size_t) t] && foldArea (t).contains (p))
        return Part::fold;

    if (t > 0 && armArea (t).expanded (1).contains (p))
        return Part::arm;

    if (t > 0 && t < (int) engine.getSong().mixer.size() && engine.getSong().mixer[(std::size_t) t].inputChannel >= 0
        && inputArea (t).expanded (0, 2).contains (p))
        return Part::input;
    if (headerArea (t).contains (p) || nameArea (t).contains (p))
        return Part::header;
    if (panArea (t).contains (p))
        return Part::pan;
    if (stereoArea (t).contains (p))
        return Part::stereo;
    if (muteArea (t).contains (p))
        return Part::mute;
    if (t > 0 && soloArea (t).contains (p))
        return Part::solo;
    if (sending && routeArea (t).contains (p) && (song.routes (selected, t) || song.canRoute (selected, t)))
        return Part::route;
    if (sending && sendArea (t).contains (p) && song.routes (selected, t))
        return Part::send;
    if (meterArea (t).contains (p))
        return Part::meter;
    if (faderRail (t).expanded (0, 6).contains (p))
        return Part::fader;

    return Part::body;
}

juce::Rectangle<int> MixerStrips::tipArea (Part part, int t) const
{
    switch (part)
    {
        case Part::pan: return panArea (t);
        case Part::stereo: return stereoArea (t);
        case Part::send: return sendArea (t);
        case Part::route: return routeArea (t);
        case Part::fader:
        {
            const auto rail = faderRail (t);
            const auto& mixer = engine.getSong().mixer;
            const auto v = t >= 0 && t < (int) mixer.size() ? shownVolume (t) : 0.0f;
            const auto capY = rail.getBottom() - (int) (v * (float) rail.getHeight());
            return { rail.getX(), capY - 6, rail.getWidth(), 12 };
        }
        default: return {};
    }
}

void MixerStrips::showTip (Part part, int t, bool transient)
{
    if (t < 0 || t >= (int) engine.getSong().mixer.size())
        return;

    const auto spec = specFor (part, t);
    const auto text = spec.text (valueOf (part, t));
    ui::postHint (ui::valueHint (spec, valueOf (part, t), ui::valueGestures()));

    if (transient)
        ui::flashValueTip (*this, tipArea (part, t), text);
    else
        ui::showValueTip (*this, tipArea (part, t), text);
}

void MixerStrips::setHover (Part part, int t)
{
    if (part == hoverPart && t == hoverTrack)
        return;

    auto dirty = [this] (int track) {
        if (track >= 0 || track <= -2) // (≤ -2: a VCA strip)
            repaint (stripX (track) - 2, 0, stripWidthOf (track) + 4, getHeight());
    };

    dirty (hoverTrack);
    hoverPart = part;
    hoverTrack = t;
    dirty (hoverTrack);
}

void MixerStrips::mouseMove (const juce::MouseEvent& e)
{
    if (const auto i = vcaAt (e.x); i >= 0)
    {
        setHover (vcaPartAt (i, e.getPosition()), vcaStrip (i));
        return;
    }
    if (const auto over = showLoudness && loudnessResetArea().contains (e.getPosition()); over != loudnessResetHovered)
    {
        loudnessResetHovered = over;
        repaint (loudnessResetArea().expanded (2));
    }
    const auto t = trackAt (e.x);
    setHover (t >= 0 ? partAt (t, e.getPosition()) : Part::none, t);
}

void MixerStrips::mouseExit (const juce::MouseEvent&)
{
    if (std::exchange (loudnessResetHovered, false))
        repaint (loudnessResetArea().expanded (2));
    setHover (Part::none, -1);
}

void MixerStrips::paint (juce::Graphics& g)
{
    const uitrace::Scope trace ("MixerStrips paint", true, g.getClipBounds());
    using namespace theme;
    const auto& song = engine.getSong();
    const auto tracks = (int) song.mixer.size();
    const auto clip = g.getClipBounds();
    // round 173f: a frame of the meters' timer repaints only the meter rectangles, but the clip's bounds span every strip
    // between them, so each strip's parts are drawn only where the frame's region (not its bounds) reaches them
    const auto needs = [&g] (juce::Rectangle<int> area) { return g.clipRegionIntersects (area.expanded (3)); };
    g.fillAll (colour::bg1);

    // the routing lane runs under all strips
    g.setColour (colour::bg0.withAlpha (0.55f));
    g.fillRect (0, laneTop() + switchHeight + sendKnobHeight + 2, getWidth(), wiresHeight + 2);

    // FL's separators (a line on the left of the track) and the gaps between the dock blocks (round 146a), drawn
    // per block so the pinned left / right blocks cover the scrolled middle (round 149)
    auto paintDividers = [&] (MixerDock dock) {
        const auto shift = pinOffset (dock);
        g.setColour (colour::borderStrong);
        for (auto [lx, d] : separatorLines)
            if (d == dock)
                theme::fillRoundRect (g, { (float) (lx + shift), 8.0f, 2.0f, (float) laneTop() - 14.0f }, 1.0f, colour::borderStrong);
        g.setColour (colour::bg0);
        for (auto [gx, d] : dockGaps)
            if (d == dock)
                g.fillRect (gx + shift - 3, 0, 6, getHeight());
    };

    for (auto pass : { MixerDock::middle, MixerDock::left, MixerDock::right })
    {
    if (pass == MixerDock::middle)
    {
        // "+" button
        if (tracks < maxMixerTracks)
        {
            const auto add = juce::Rectangle<int> (stripX (tracks), 2, spec().stripWidth - spec().gap, 60).toFloat();
            const auto addHovered = hoverPart == Part::add;
            theme::fillRoundRect (g, add, radius::md, addHovered ? colour::bg4 : colour::bg2);
            g.setColour (addHovered ? colour::borderStrong : colour::border);
            g.drawRoundedRectangle (add.reduced (0.5f), radius::md, 1.0f);
            icons::draw (g, Icon::plus, add.withSizeKeepingCentre (22.0f, 22.0f), addHovered ? colour::textPrimary : colour::textSecondary);
        }
    }
    else if (const auto shift = pinOffset (pass); shift != 0) // a pinned block hides what scrolls under it
    {
        const auto d = (int) pass;
        g.setColour (colour::bg1);
        g.fillRect (blockStart[d] + shift, 0, blockEnd[d] - blockStart[d], laneTop());
        g.setColour (colour::bg0.withAlpha (0.55f));
        g.fillRect (blockStart[d] + shift, laneTop(), blockEnd[d] - blockStart[d], getHeight() - laneTop());
    }

    for (int t = 0; t < tracks; ++t)
    {
        if (song.mixer[(std::size_t) t].dock != pass || isHidden (t))
            continue;

        const auto x = stripX (t);
        const auto w = stripWidthOf (t);

        if (x > clip.getRight() || x + w < clip.getX())
            continue;

        if (isFoldTab (t))
        {
            paintFoldTab (g, t);
            continue;
        }

        const auto& track = song.mixer[(std::size_t) t];
        const auto& sp = spec();
        const auto plateWidth = w - sp.gap;
        const auto compactLayout = mixerlayout::isCompact (layout);
        const auto strip = juce::Rectangle<int> (x, 2, plateWidth, laneTop() - 2);
        const auto isSelected = t == selected;
        const auto inSelection = std::binary_search (selection.begin(), selection.end(), t);
        const auto stripHovered = hoverTrack == t;
        const auto hovered = [this, t] (Part p) { return hoverTrack == t && hoverPart == p; };
        const auto dragging = [this, t] (Part p) { return dragTrack == t && dragPart == p; };

        // the strip is tinted with the insert's colour (FL tints whole strips, so groups read at a glance);
        // kept dark so meters, knobs and text keep their contrast
        auto plate = isSelected || inSelection ? colour::bg4 : (t == 0 ? colour::bg3 : stripHovered ? colour::bg3 : colour::bg2);
        if (track.colour != 0)
            plate = plate.interpolatedWith (tame (juce::Colour (track.colour)), isSelected ? 0.26f : 0.2f);
        theme::fillRoundRect (g, strip.toFloat(), radius::md, plate);

        if (track.colour != 0) // thin colour band at the top
        {
            theme::fillRoundRect (g, strip.toFloat().withHeight (3.0f).reduced (6.0f, 0.0f), 1.5f, tame (juce::Colour (track.colour)));
        }

        if (inSelection && ! isSelected)
        {
            g.setColour (colour::accent.withAlpha (0.3f));
            g.drawRoundedRectangle (strip.toFloat().reduced (0.5f), radius::md, 1.0f);
        }

        if (isSelected)
        {
            g.setColour (colour::accent.withAlpha (0.55f));
            g.drawRoundedRectangle (strip.toFloat().reduced (0.5f), radius::md, 1.0f);
            theme::fillRoundRect (g, strip.toFloat().withHeight (3.0f).reduced (6.0f, 0.0f), 1.5f, colour::accent);
        }

        if (headerDragged && headerDragTrack == t)
        {
            g.setColour (colour::accent);
            g.drawRoundedRectangle (strip.toFloat().reduced (0.5f), radius::md, 1.5f);
        }

        // record arm: the input recording goes through this insert
        if (t > 0 && needs (armArea (t).getUnion (inputArea (t))))
        {
            const auto armed = engine.isInsertArmed (t);
            const auto r = armArea (t).toFloat().reduced (2.0f);
            g.setColour (armed ? colour::record : hovered (Part::arm) ? colour::bg5 : colour::bg0);
            g.fillEllipse (r);
            g.setColour (armed ? colour::record.brighter (0.3f) : hovered (Part::arm) ? colour::textSecondary : colour::borderStrong);
            g.drawEllipse (r, 1.0f);
            if (track.recordSafe) // record safe: a slash through the arm button
            {
                g.setColour (colour::textDisabled);
                g.drawLine (r.getX() + 1.0f, r.getBottom() - 1.0f, r.getRight() - 1.0f, r.getY() + 1.0f, 1.2f);
            }

            // the input level (inserts with an audio input source)
            if (track.inputChannel >= 0 && (std::size_t) t < inputLevels.size())
            {
                const auto bar = inputArea (t).toFloat();
                theme::fillRoundRect (g, bar, 1.0f, colour::bg0);
                const auto level = inputLevels[(std::size_t) t];
                const auto fill = juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (level, -60.0f) + 60.0f) / 60.0f);
                theme::fillRoundRect (g, bar.withWidth (bar.getWidth() * fill), 1.0f, level >= 0.99f ? colour::record : colour::meterLow);
            }
        }

        // header: number and effect count on one line, the icon under them (FL's place), then the name: on the
        // header's last row (Extra large) or up a column of the strip (round 168h)
        if (needs (juce::Rectangle<int> (x, 0, plateWidth, 18).getUnion (iconArea (t))))
        {
        g.setColour (isSelected ? colour::accent : colour::textSecondary);
        g.setFont (font (compactLayout ? 9.5f : type::caption, isSelected));
        textcache::drawFittedText (g, t == 0 ? juce::String ("M") : juce::String (t), juce::Rectangle<int> (x, 4, plateWidth, 13),
                                   juce::Justification::centred, 1, 0.6f);

        if (t < (int) foldHead.size() && foldHead[(std::size_t) t]) // round 177a: fold / unfold the group
        {
            const auto folded = mixerops::isFolded (song, t);
            const auto box = foldArea (t).toFloat();
            const auto c = box.getCentre();
            icons::draw (g, Icon::chevronDown,
                         juce::Rectangle<float> (10.0f, 10.0f).withCentre (c).transformedBy (
                             juce::AffineTransform::rotation ((folded ? -1.0f : 1.0f) * juce::MathConstants<float>::halfPi, c.x, c.y)),
                         hovered (Part::fold) ? colour::textPrimary : colour::textSecondary);
        }

        if (const auto icon = trackicons::iconFor (track.icon); icon != trackicons::Icon::none)
            ui::drawTrackIcon (g, icon, iconArea (t).toFloat(), track.muted ? colour::textDisabled : colour::textPrimary.withAlpha (0.88f));

        if (! track.effects.empty() && sp.fxBadge)
        {
            const auto on = mixerops::anyEffectEnabled (song, t);
            g.setColour (on ? colour::info : colour::textDisabled);
            g.setFont (font (9.0f, true));
            if (plateWidth >= 56)
                textcache::drawText (g, "FX" + juce::String ((int) track.effects.size()), x + plateWidth - 26, 4, 23, 13, juce::Justification::centredRight);
            else // narrow strips: a dot at the number's right
                g.fillEllipse ((float) (x + plateWidth) - 7.0f, 8.0f, 4.0f, 4.0f);
        }
        }

        if (needs (nameArea (t).withX (x).withWidth (plateWidth)))
        {
        g.setColour (track.muted ? colour::textDisabled : (hovered (Part::header) ? colour::textPrimary : colour::textPrimary.withAlpha (0.9f)));
        g.setFont (font (compactLayout ? 10.0f : type::small, isSelected));
        // Online Sessions (round 168a): a plugin of this insert is not installed here, its stand-in render plays (an
        // amber R before the name)
        const auto standIn = engine.insertPlaysStandIn (t);
        // round 190ap: opened without third-party plugins, a plugin of this insert is still off (a grey power tag)
        const auto pluginOff = ! standIn && engine.insertPluginOff (t);
        const auto name = juce::String::fromUTF8 (track.name.c_str());
        if (const auto box = nameArea (t); ! sp.verticalName)
        {
            textcache::drawFittedText (g, name, box.withTrimmedLeft (standIn || pluginOff ? 16 : 0), juce::Justification::centred, 1, 0.7f);
            if (standIn)
                drawStandInTag (g, { (float) box.getX() + 1.0f, (float) box.getY() + 0.5f, 13.0f, 13.0f });
            else if (pluginOff)
                ui::drawPluginOffTag (g, { (float) box.getX() + 1.0f, (float) box.getY() + 0.5f, 13.0f, 13.0f });
        }
        else
        {
            // the name reads bottom to top, as FL's narrow strips do
            auto column = box.reduced (2, 4);
            if (standIn)
            {
                drawStandInTag (g, juce::Rectangle<float> (13.0f, 13.0f).withCentre ({ (float) column.getCentreX(), (float) column.getBottom() - 6.5f }));
                column.removeFromBottom (16);
            }
            else if (pluginOff)
            {
                ui::drawPluginOffTag (g, juce::Rectangle<float> (13.0f, 13.0f).withCentre ({ (float) column.getCentreX(), (float) column.getBottom() - 6.5f }));
                column.removeFromBottom (16);
            }
            juce::Graphics::ScopedSaveState state (g);
            g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, (float) column.getCentreX(), (float) column.getCentreY()));
            const auto rotated = juce::Rectangle<int> (column.getHeight(), column.getWidth()).withCentre (column.getCentre());
            textcache::drawFittedText (g, name, rotated, juce::Justification::centredLeft, 1, 0.8f); // (laid out once: round 173f)
        }

        // FL's narrow strips show the level beside the name too: two thin bars (Wide layouts)
        if (const auto bars = nameBars (t); ! bars.isEmpty())
        {
            theme::fillRoundRect (g, bars.toFloat(), 2.0f, colour::bg0.withAlpha (0.8f));
            if (! cachePainting) // (cached: drawn over the picture, paintMeterLevels)
                paintNameBars (g, t);
        }
        }

        // compensated latency (PDC) on the Master
        if (t == 0 && engine.getLatencySamples() > 0 && ! compactLayout && needs (soloArea (t).withX (x).withWidth (w)))
        {
            const auto ms = 1000.0 * engine.getLatencySamples() / juce::jmax (1.0, engine.getTransport().sampleRate.load());
            g.setColour (colour::textSecondary);
            g.setFont (font (10.0f));
            textcache::drawFittedText (g, "PDC " + juce::String (ms, 1) + " ms", soloArea (t).withX (x + 30).withWidth (w - 36),
                              juce::Justification::centredLeft, 1, 0.8f); // the Master has no solo: use its spot
        }

        // pan and stereo separation
        const auto knob = (float) sp.knobSize;
        const auto stereoKnob = sp.stackedKnobs ? knob - 6.0f : 22.0f;
        if (needs (panArea (t).getUnion (stereoArea (t))))
        {
        drawKnob (g, panArea (t).toFloat().withSizeKeepingCentre (knob, knob),
                  engine.getAutomationDisplay().shown (AutomationTarget { AutomationTargetKind::mixerPan, t }, (track.pan + 1.0f) * 0.5f), true,
                  dragging (Part::pan) ? colour::accent : colour::info, hovered (Part::pan) || dragging (Part::pan));
        if (sp.stereoKnob)
            drawKnob (g, stereoArea (t).toFloat().withSizeKeepingCentre (stereoKnob, stereoKnob), (track.stereo + 1.0f) * 0.5f, true,
                      dragging (Part::stereo) ? colour::accent : colour::knobArc, hovered (Part::stereo) || dragging (Part::stereo));
        }

        // mute and solo
        if (needs (muteArea (t).getUnion (soloArea (t))))
        {
            const auto led = compactLayout ? 10.0f : 12.0f;
            const auto m = muteArea (t).toFloat().withSizeKeepingCentre (led, led);
            g.setColour (muteLed (track.muted));
            g.fillEllipse (m);
            if (! track.muted || hovered (Part::mute))
            {
                g.setColour (hovered (Part::mute) ? colour::textSecondary : colour::ledOn.withAlpha (0.25f));
                g.drawEllipse (m.expanded (2.0f), 1.0f);
            }

            if (t > 0)
            {
                const auto s = soloArea (t).toFloat().reduced (2.0f);
                theme::fillRoundRect (g, s, radius::sm, track.solo ? colour::accent : hovered (Part::solo) ? colour::bg5 : colour::bg0);
                g.setColour (track.solo ? colour::onAccent : hovered (Part::solo) ? colour::textPrimary : colour::textSecondary);
                g.setFont (font (type::caption, true));
                textcache::drawText (g, "S", s.toNearestInt(), juce::Justification::centred);
            }
        }

        // meters + fader
        {
            const auto area = faderArea (t);
            const auto meters = meterArea (t);
            const auto fader = faderRail (t);

            for (int ch = 0; ch < 2; ++ch)
            {
                const auto bar = juce::Rectangle<int> (meters.getX() + ch * (sp.meterWidth + 2), meters.getY(), sp.meterWidth, meters.getHeight());
                theme::fillRoundRect (g, bar.toFloat(), 1.5f, colour::bg0);
                if (! cachePainting) // (cached: the levels are drawn over the picture, paintMeterLevels)
                    paintMeterLevel (g, t, ch, bar);
            }

            if (hovered (Part::meter))
            {
                g.setColour (colour::textSecondary.withAlpha (0.5f));
                g.drawRect (meters.expanded (1), 1);
            }

            // round 177d: gain reduction of the strip's own dynamics, hanging down from the top (deeper = more)
            if (const auto gr = gainReduction (t); gr >= 0.0f)
            {
                const auto bar = grArea (t);
                g.setColour (colour::bg0.withAlpha (0.6f));
                g.fillRect (bar);
                g.setColour (colour::meterMid);
                g.fillRect (bar.withHeight (juce::roundToInt (gr * (float) bar.getHeight())));
            }

            // the master's dB scale (FL prints it beside the master meter): as many values as fit without overlapping
            if (t == 0)
            {
                const auto scaleFont = font (9.0f);
                const auto textHeight = 10.0f;
                const auto left = (float) meters.getRight() + 1.0f;
                g.setFont (scaleFont);

                for (auto db : lynx::meters::scaleMarks ((float) meters.getHeight(), textHeight + 1.0f))
                {
                    const auto y = (float) meters.getBottom() - lynx::meters::scalePosition ((float) db) * (float) meters.getHeight();
                    g.setColour (db == 0 ? colour::textPrimary : colour::textSecondary);
                    g.fillRect (juce::Rectangle<float> (left, y - 0.5f, 2.0f, 1.0f));
                    const auto box = juce::Rectangle<float> (left + 2.0f, y - textHeight * 0.5f, (float) masterScaleWidth - 2.0f, textHeight)
                                         .withY (juce::jlimit ((float) meters.getY(), (float) meters.getBottom() - textHeight, y - textHeight * 0.5f));
                    textcache::drawText (g, (db > 0 ? "+" : "") + juce::String (db), box, juce::Justification::centredRight, false);
                }
            }

            // fader track and 0 dB mark
            const auto railX = (float) fader.getCentreX();
            const auto volume = shownVolume (t); // (its automation's value while it plays: the fader moves, issue #12)
            const auto capY = (float) fader.getBottom() - volume * (float) fader.getHeight();
            theme::fillRoundRect (g, { railX - 2.0f, (float) fader.getY(), 4.0f, (float) fader.getHeight() }, 2.0f, colour::bg0);
            theme::fillRoundRect (g, { railX - 2.0f, capY, 4.0f, juce::jmax (0.0f, (float) fader.getBottom() - capY) }, 2.0f, colour::bg6.brighter (0.15f).withAlpha (track.muted ? 0.4f : 1.0f));
            const auto unityY = (float) fader.getBottom() - unityFader * (float) fader.getHeight();
            g.setColour (colour::textDisabled);
            g.drawHorizontalLine ((int) unityY, (float) fader.getX(), railX - 5.0f);
            g.drawHorizontalLine ((int) unityY, railX + 5.0f, (float) fader.getRight());

            const auto capActive = hovered (Part::fader) || dragging (Part::fader);
            const auto capWidth = juce::jmin ((float) fader.getWidth() + 2.0f, 26.0f);
            const auto cap = juce::Rectangle<float> (capWidth, 12.0f).withCentre ({ railX, capY });
            theme::fillRoundRect (g, cap.translated (0.0f, 1.5f), radius::sm, juce::Colours::black.withAlpha (0.35f));
            // same cap as the LookAndFeel's linear faders
            theme::fillRoundRect (g, cap, radius::sm, capActive ? juce::Colour (0xffffffff) : juce::Colour (0xffd4d7dd));
            g.setColour ((dragging (Part::fader) ? colour::accent : colour::bg0).withAlpha (0.8f));
            g.fillRect (cap.withSizeKeepingCentre (capWidth - 8.0f, 1.5f));

            // a VCA member (round 177a): a tick where the level that plays sits (the fader times its VCAs)
            if (t > 0 && ! song.vcas.empty())
                if (const auto gain = mixerops::vcaGain (song, t); gain != 1.0f)
                {
                    const auto playing = faderPosition (faderGain (volume) * gain);
                    const auto tickY = (float) fader.getBottom() - playing * (float) fader.getHeight();
                    g.setColour (colour::info);
                    g.fillRect (juce::Rectangle<float> (railX - 7.0f, tickY - 1.0f, 14.0f, 2.0f));
                }

            g.setColour (colour::textSecondary);
            g.setFont (font (type::caption));
            if (sp.dbReadout)
                textcache::drawFittedText (g, faderToDb (volume) + " dB", juce::Rectangle<int> (x, area.getBottom() + 3, plateWidth, 14), juce::Justification::centred, 1, 0.7f);

            // a hardware output target (REA round 156, optional): a small badge at the fader's top right
            if (mixeroutput::isCustom (engine, t))
            {
                const auto text = mixeroutput::label (engine, t, true);
                const auto badgeLeft = (float) meterArea (t).getRight() + 3.0f + (t == 0 ? (float) masterScaleWidth : 0.0f);
                const auto badge = juce::Rectangle<float> (badgeLeft, (float) area.getY(), (float) (x + w - 6) - badgeLeft, 12.0f);
                theme::fillRoundRect (g, badge, 3.0f, colour::bg0.withAlpha (0.85f));
                g.setColour (text.endsWith ("?") ? colour::textDisabled : colour::info);
                g.setFont (font (9.0f, true));
                textcache::drawFittedText (g, text, badge.toNearestInt(), juce::Justification::centred, 1, 0.7f);
            }
        }

        // round 186c's option: the selected insert's sends to deleted inserts, as a greyed dashed chip in its own (empty)
        // route-switch place: the deleted insert's name (or how many); the insert menu's "Disconnected links" acts on them
        if (t == selected && t > 0 && ! song.mixer[(std::size_t) t].lostSends.empty() && needs (routeArea (t)))
        {
            const auto& lost = song.mixer[(std::size_t) t].lostSends;
            const auto r = routeArea (t).toFloat().reduced (0.5f);
            juce::Path outline, dashed;
            outline.addRoundedRectangle (r, radius::sm);
            const float dashes[] = { 2.5f, 2.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dashes, 2);
            g.setColour (colour::textDisabled);
            g.fillPath (dashed);
            g.setFont (font (10.0f));
            textcache::drawFittedText (g, juce::String::fromUTF8 ("\xc3\x97 ") + (lost.size() == 1 ? juce::String::fromUTF8 (lost[0].to.c_str()) : juce::String ((int) lost.size())),
                                       r.toNearestInt().reduced (2, 0), juce::Justification::centred, 1, 0.6f);
        }

        // routing lane: send switch from the selected insert to this one, and its level knob
        if (selected > 0 && t != selected && selected < tracks && needs (routeArea (t).getUnion (sendArea (t))))
        {
            const auto r = routeArea (t).toFloat();
            const auto on = song.routes (selected, t);
            const auto possible = on || song.canRoute (selected, t);
            const auto feedsSelected = t > 0 && song.routes (t, selected);

            if (possible)
            {
                theme::fillRoundRect (g, r, radius::sm, on ? colour::accent : hovered (Part::route) ? colour::bg5 : colour::bg0);
                if (hovered (Part::route))
                {
                    g.setColour (on ? colour::accentHover : colour::textSecondary);
                    g.drawRoundedRectangle (r.reduced (0.5f), radius::sm, 1.0f);
                }
                g.setColour (on ? colour::onAccent : colour::textSecondary);
                g.setFont (font (10.0f, on));
                textcache::drawText (g, (compactLayout ? juce::String() : juce::String::fromUTF8 ("\xe2\x86\x92 ")) + (t == 0 ? juce::String ("M") : juce::String (t)), r.toNearestInt(),
                            juce::Justification::centred);

                // the route's level knob (FL's route knob), on the routes to inserts and to Master
                if (on)
                    drawKnob (g, sendArea (t).toFloat().withSizeKeepingCentre (compactLayout ? 16.0f : 22.0f, compactLayout ? 16.0f : 22.0f),
                              engine.getAutomationDisplay().shown (AutomationTarget { AutomationTargetKind::mixerSend, selected, t },
                                                                   faderPosition (song.routeLevel (selected, t))),
                              false,
                              dragging (Part::send) ? colour::accent : colour::accentHover, hovered (Part::send) || dragging (Part::send));
                // a pre-fader send (LYNX's switch, round 190aq) is marked beside its knob
                if (on && t > 0 && song.sendPre (selected, t))
                {
                    // (its whole text, ending at the knob's left edge: the strip's margin is narrower than "PRE")
                    const auto area = sendArea (t);
                    const auto mark = compactLayout ? juce::String ("P") : tr ("PRE");
                    g.setColour (colour::accentHover);
                    g.setFont (font (8.0f, true));
                    const auto width = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), mark) + 2;
                    const auto knobLeft = area.getCentreX() - (compactLayout ? 8 : 11);
                    textcache::drawText (g, mark, juce::Rectangle<int> (knobLeft - width - 1, area.getY(), width, area.getHeight()),
                                         juce::Justification::centredRight, false);
                }
            }
            else if (feedsSelected) // this insert sends into the selected one
            {
                theme::fillRoundRect (g, r, radius::sm, colour::info.withAlpha (0.18f));
                g.setColour (colour::info);
                g.setFont (font (10.0f, true));
                textcache::drawText (g, (compactLayout ? juce::String() : juce::String::fromUTF8 ("\xe2\x86\x92 ")) + juce::String (selected), r.toNearestInt(), juce::Justification::centred);
            }
        }
    }
    if (pass == dockOf (0))
    {
        for (int i = 0; i < (int) vcaXs.size(); ++i)
            paintVcaStrip (g, i);
        if (showLoudness && g.clipRegionIntersects (loudnessArea().expanded (3)))
        {
            paintLoudnessPanel (g);
            if (! cachePainting) // (cached: drawn over the picture, paintMeterLevels)
                paintLoudnessValues (g);
        }
    }
    paintDividers (pass);
    }

    paintRouting (g);
}

// Wires in the lane under the strips: from the selected insert to where it sends (accent), and from the
// inserts that feed it (blue). The Master output is drawn dimmer.
void MixerStrips::paintRouting (juce::Graphics& g)
{
    using namespace theme;
    const auto& song = engine.getSong();
    const auto tracks = (int) song.mixer.size();

    if (selected <= 0 || selected >= tracks)
        return;

    const auto top = (float) (laneTop() + switchHeight + sendKnobHeight + 3);
    const auto bottom = top + (float) wiresHeight - 2.0f;
    const auto centre = [this] (int t) { return (float) stripX (t) + (float) (stripWidthOf (t) - 4) * 0.5f; };

    auto wire = [&] (int from, int to, juce::Colour c, float thickness) {
        const auto x0 = centre (from), x1 = centre (to);
        juce::Path p;
        p.startNewSubPath (x0, top);
        p.cubicTo (x0, bottom + 2.0f, x1, bottom + 2.0f, x1, top);
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.fillEllipse (x1 - 2.5f, top - 2.5f, 5.0f, 5.0f);
    };

    const auto& from = song.mixer[(std::size_t) selected];

    for (int t = 1; t < tracks; ++t)
        if (t != selected && song.routes (t, selected))
            wire (t, selected, colour::info.withAlpha (0.85f), 1.5f);

    if (from.toMaster)
        wire (selected, 0, colour::accent.withAlpha (0.4f), 1.0f);

    for (const auto& send : from.sends)
        if (send.target > 0 && send.target < tracks)
            wire (selected, send.target, colour::accent.withAlpha (0.5f + 0.5f * juce::jlimit (0.0f, 1.0f, send.level)), 1.8f);

    g.setColour (colour::accent);
    g.fillEllipse (centre (selected) - 3.0f, top - 3.0f, 6.0f, 6.0f);
}

void MixerStrips::mouseDown (const juce::MouseEvent& e)
{
    if (const auto i = vcaAt (e.x); i >= 0)
    {
        vcaMouseDown (e, i);
        return;
    }
    if (showLoudness && loudnessArea().contains (e.getPosition())) // round 190am: Reset, or the panel's menu
    {
        if (flmods::popup (e.mods))
            showLoudnessMenu();
        else if (loudnessResetArea().contains (e.getPosition()))
            resetLoudness();
        return;
    }

    const auto t = trackAt (e.x);

    if (t < 0)
        return;

    const auto part = partAt (t, e.getPosition());
    headerDragTrack = -1;
    headerDragged = false;

    if (part == Part::add && flmods::popup (e.mods))
    {
        // FL's "+" menu (round 166d [SHOT]): Add one / Add multiple...
        juce::PopupMenu menu;
        const auto count = (int) engine.getSong().mixer.size();
        const auto room = mixerops::roomForInserts (engine.getSong()) > 0;
        menu.addItem (tr ("Add one"), room, false, [this, count] {
            engine.editSong ([count] (Song& s) { mixerops::insertTracks (s, juce::jmax (1, count), 1); });
            revealed.insert (juce::jmax (1, count));
            updateSize();
            selectTrack (juce::jmax (1, count));
        });
        menu.addItem (tr ("Add multiple..."), room, false, [this, count] { askInsertMultiple (juce::jmax (1, count)); });
        // FL's "Add from state" (REA round 167q: AddMixerTracksMenu, FUN_012a87d0 / FUN_012a83b0)
        juce::PopupMenu fromState;
        for (const auto& f : mixerpresets::all())
            fromState.addItem (f.getFileNameWithoutExtension(), room, false, [this, count, f] {
                if (const auto added = mixerpresets::insertTrack (engine, juce::jmax (1, count), f); added > 0)
                {
                    revealed.insert (added);
                    updateSize();
                    selectTrack (added);
                }
            });
        menu.addSubMenu (tr ("Add from state"), fromState, room && fromState.getNumItems() > 0);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
        return;
    }

    if (part == Part::add)
    {
        if (showUsedOnly) // the first unused insert (hidden) comes back before a new one is added
            if (const auto free = engine.getSong().nextFreeInsert(); free > 0 && isHidden (free))
            {
                revealed.insert (free);
                updateSize();
                selectTrack (free);
                return;
            }
        const auto added = (int) engine.getSong().mixer.size();
        engine.editSong ([] (Song& s) {
            if (s.mixer.empty())
                s.mixer = Song::defaultMixer (0);

            if ((int) s.mixer.size() >= maxMixerTracks)
                return;
            auto& track = s.mixer.emplace_back();
            track.name = "Insert " + std::to_string (s.mixer.size() - 1);
        });
        if (added < (int) engine.getSong().mixer.size())
        {
            revealed.insert (added); // shown even while unused
            updateSize();
            selectTrack (added);
        }
        return;
    }

    if ((part == Part::fold || part == Part::tab) && ! flmods::popup (e.mods))
    {
        // round 177a: fold / unfold the group; Alt: every group (to the state this one takes)
        const auto all = e.mods.isAltDown();
        engine.editSong ([t, all] (Song& s) {
            const auto folded = mixerops::toggleFold (s, t);
            if (all)
                mixerops::foldAll (s, folded);
        });
        updateSize();
        repaint();
        return;
    }

    if (part == Part::arm)
    {
        if (flmods::popup (e.mods)) // FL: "Record <insert> to" (round 167a); the input box keeps the input menu
            mixerinput::chooseRecordTo (engine, t);
        else
            mixerinput::toggleArm (engine, t, this);
        repaint();
        return;
    }

    if (part == Part::input)
    {
        mixerinput::showMenu (engine, t, this);
        return;
    }

    if (part == Part::meter && ! flmods::popup (e.mods)) // clears the clip latch and the peak hold, and selects (as FL)
    {
        for (int ch = 0; ch < 2; ++ch)
            if (const auto i = (std::size_t) t * 2 + (std::size_t) ch; i < levels.size())
                levels[i].resetClip();
        repaint (meterArea (t).expanded (2));
    }

    if (part == Part::route && flmods::popup (e.mods))
    {
        showSendMenu (t);
        return;
    }

    if (part == Part::send)
    {
        const auto from = selected;
        if (flmods::popup (e.mods))
        {
            juce::PopupMenu menu;
            const auto spec = specFor (Part::send, t);
            menu.addSectionHeader (spec.name + ": " + spec.text (valueOf (Part::send, t)));
            ui::addValueItems (menu, *this, &engine, spec, valueOf (Part::send, t), [this, t] (double v) { setValue (Part::send, t, v); });
            menu.addSeparator();
            addPreFaderItem (menu, from, t);
            menu.addItem (t == 0 ? tr ("Remove the route to Master") : tr ("Remove this send"),
                          [this, from, t] { engine.editSong ([from, t] (Song& s) { s.setRoute (from, t, false); }); });
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
            return;
        }

        if (ui::isResetClick (e))
        {
            setValue (Part::send, t, unityFader); // 0 dB
            showTip (Part::send, t, true);
            return;
        }

        dragPart = Part::send;
        dragTrack = t;
        drag.begin (e, valueOf (Part::send, t));
        showTip (Part::send, t, false);
        return;
    }

    if (part == Part::mute && (flmods::popup (e.mods) || flmods::ctrl (e.mods))) // solo on the same LED
    {
        soloExclusive (t);
        return;
    }

    if (part == Part::route)
    {
        // the selection is the send source: don't change it. FL (round 170b, REA FUN_012aa110): the arrow acts on every
        // selected track; Ctrl = route to this track only, Shift = sidechain, Ctrl+Shift = sidechain only.
        const auto from = selected;
        const auto sources = selection;
        using K = mixerops::RouteKind;
        if (flmods::ctrl (e.mods) || e.mods.isShiftDown())
        {
            routeSelection (t, flmods::ctrl (e.mods) ? (e.mods.isShiftDown() ? K::sidechainOnly : K::routeOnly) : K::sidechain);
            return;
        }
        const auto on = engine.getSong().routes (from, t);
        engine.editSong ([sources, t, on] (Song& s) { mixerops::switchSelectedRoute (s, sources, t, ! on); });

        if (! on && engine.getSong().routes (from, t)) // just enabled (at 0 dB): dragging adjusts the level, like the route knob
        {
            dragPart = Part::send;
            dragTrack = t;
            drag.begin (e, unityFader);
        }
        return;
    }

    if ((part == Part::body || part == Part::header || part == Part::meter) && ! flmods::popup (e.mods) && flmods::ctrl (e.mods) && t > 0)
    {
        // FL (round 170b, REA FUN_012c5800 / FUN_012c5f60): Ctrl+click (and drag across the strips) selects the tracks
        // from here to the mouse, alone; Ctrl+Shift adds them to the selection, or removes them when the click starts
        // on a selected track
        selectAnchor = t;
        selectAdding = ! e.mods.isShiftDown() || ! std::binary_search (selection.begin(), selection.end(), t);
        selectBase.clear();
        if (e.mods.isShiftDown())
            selectBase = selection;
        dragSelectTo (t);
        return;
    }

    if ((part == Part::body || part == Part::header || part == Part::meter) && ! flmods::popup (e.mods) && e.mods.isShiftDown() && t > 0)
    {
        auto s = selection;
        {
            s.clear();
            for (int i = juce::jmin (selected, t); i <= juce::jmax (selected, t); ++i)
                s.push_back (i);
        }

        setSelection (s, std::find (s.begin(), s.end(), t) != s.end() ? t : s.front());
        return;
    }

    // a plain click on a track outside the selection selects only it; inside, the group stays
    if (! std::binary_search (selection.begin(), selection.end(), t))
        setSelection ({ t }, t);
    else if (selected != t)
        setSelection (selection, t);

    if (part == Part::pan || part == Part::fader || part == Part::stereo)
    {
        if (flmods::popup (e.mods))
        {
            ui::showValueMenu (*this, part == Part::stereo ? nullptr : &engine, specFor (part, t), valueOf (part, t),
                               [this, part, t] (double v) { setValue (part, t, v); });
            return;
        }

        if (ui::isResetClick (e))
        {
            setValue (part, t, specFor (part, t).defaultValue);
            showTip (part, t, true);
            return;
        }
    }

    if (flmods::popup (e.mods))
    {
        showMenu (t);
        return;
    }

    const auto& track = engine.getSong().mixer[(std::size_t) t];

    switch (part)
    {
        case Part::mute:
            engine.editSong ([t] (Song& s) { s.mixer[(std::size_t) t].muted = ! s.mixer[(std::size_t) t].muted; });
            return;

        case Part::solo:
            engine.editSong ([t] (Song& s) { s.mixer[(std::size_t) t].solo = ! s.mixer[(std::size_t) t].solo; });
            return;

        case Part::header:
            if (t > 0)
                headerDragTrack = t; // dragging sideways reorders
            return;

        case Part::stereo:
            dragPart = part;
            dragTrack = t;
            dragStartValue = track.stereo;
            drag.begin (e, dragStartValue);
            groupStart.clear();
            showTip (part, t, false);
            return;

        case Part::pan:
        case Part::fader:
            dragPart = part;
            dragTrack = t;
            dragStartValue = part == Part::pan ? track.pan : track.volume;

            groupStart.clear();
            groupGrab = dragStartValue;
            if (selection.size() > 1) // the selected strips follow, keeping their offsets
                for (auto s : selection)
                    if (s != t && s < (int) engine.getSong().mixer.size())
                        groupStart.push_back ({ s, part == Part::pan ? engine.getSong().mixer[(std::size_t) s].pan
                                                                      : engine.getSong().mixer[(std::size_t) s].volume });

            if (part == Part::fader && ! flmods::ctrl (e.mods)) // a press on the rail steps the fader towards the mouse
            {
                const auto area = faderRail (t);
                const auto capY = area.getBottom() - track.volume * (float) area.getHeight();

                if (std::abs ((float) e.y - capY) > 9.0f)
                {
                    pressRail (t, e.y);
                    showTip (part, t, false);
                    return;
                }
            }

            drag.begin (e, dragStartValue);
            showTip (part, t, false);
            return;

        case Part::arm:
        case Part::route:
        case Part::send:
        case Part::meter:
        case Part::body:
        case Part::none:
        case Part::add:
            return;
    }
}

void MixerStrips::dragSelectTo (int t)
{
    const auto count = (int) engine.getSong().mixer.size();
    if (selectAnchor < 1 || count < 2)
        return;
    t = juce::jlimit (1, count - 1, t);
    std::set<int> s (selectBase.begin(), selectBase.end());
    for (int i = juce::jmin (selectAnchor, t); i <= juce::jmax (selectAnchor, t); ++i)
    {
        if (isHidden (i) && i != t && i != selectAnchor)
            continue;
        if (selectAdding)
            s.insert (i);
        else
            s.erase (i);
    }
    if (s.empty())
        s.insert (t);
    // the track under the mouse becomes the current one (FL); when removing, a track that stays selected
    auto primary = t;
    if (! s.count (primary))
        primary = s.count (selected) ? selected : *s.rbegin();
    setSelection ({ s.begin(), s.end() }, primary);
}

void MixerStrips::pressRail (int strip, int mouseY)
{
    railStrip = strip;
    railMouseY = mouseY;
    railPressMs = juce::Time::getMillisecondCounterHiRes();
    stepRailOnce();
    railSteps = 1;
    railRepeat.startTimer (16); // (a poll: the steps themselves follow the time since the press)
}

float MixerStrips::railTarget (int strip) const
{
    const auto rail = faderRail (strip);
    return juce::jlimit (0.0f, 1.0f, (float) (rail.getBottom() - railMouseY) / (float) rail.getHeight());
}

void MixerStrips::stepHeldRail()
{
    if (! railRepeat.isTimerRunning()) // (a direct call, as the offscreen check makes: one step)
    {
        stepRailOnce();
        return;
    }
    // the repeat stops as soon as the button is up, even before the release reaches us
    if (! juce::ModifierKeys::getCurrentModifiersRealtime().isAnyMouseButtonDown())
    {
        railRepeat.stopTimer();
        return;
    }
    const auto due = faderRailStepsForHold ((int) (juce::Time::getMillisecondCounterHiRes() - railPressMs));
    for (int guard = 0; railSteps < due && guard < 64; ++guard, ++railSteps)
        stepRailOnce();
}

void MixerStrips::stepRailOnce()
{
    const auto& song = engine.getSong();
    if (railStrip <= vcaStrip (0))
    {
        const auto i = -2 - railStrip;
        if (i < (int) song.vcas.size())
        {
            setVcaLevel (i, faderRailStepTowards (song.vcas[(std::size_t) i].level, railTarget (railStrip)));
            ui::showValueTip (*this, faderRail (railStrip), vcaSpec (i).text (vcaShownLevel (i)));
        }
    }
    else if (railStrip >= 0 && railStrip < (int) song.mixer.size())
    {
        setValue (Part::fader, railStrip, faderRailStepTowards (song.mixer[(std::size_t) railStrip].volume, railTarget (railStrip)));
        showTip (Part::fader, railStrip, false);
    }
}

void MixerStrips::mouseDrag (const juce::MouseEvent& e)
{
    if (railStrip != railNone) // the cap goes on towards where the mouse is now
    {
        railMouseY = e.y;
        return;
    }

    if (selectAnchor > 0)
    {
        if (const auto t = trackAt (juce::jlimit (0, getWidth() - 1, e.x)); t >= 0)
            dragSelectTo (t);
        return;
    }

    if (headerDragTrack > 0)
    {
        if (! headerDragged && std::abs (e.getDistanceFromDragStartX()) < 6)
            return;

        headerDragged = true;
        setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
        const auto count = (int) engine.getSong().mixer.size();
        const auto target = trackAt (e.x);
        if (target < 1 || target >= count)
            return;

        for (int guard = 0; guard < count && selected != target; ++guard)
            if (! moveSelected (target < selected ? -1 : 1))
                break;

        headerDragTrack = selected;
        repaint();
        return;
    }

    if (dragVca >= 0)
    {
        setVcaLevel (dragVca, drag.update (e, vcaSpec (dragVca)));
        ui::showValueTip (*this, faderRail (vcaStrip (dragVca)), vcaSpec (dragVca).text (vcaShownLevel (dragVca)));
        return;
    }

    if (dragPart == Part::none)
        return;

    const auto t = dragTrack;

    setValue (dragPart, t, drag.update (e, specFor (dragPart, t)));
    showTip (dragPart, t, false);
}

ui::ValueSpec MixerStrips::specFor (Part part, int t) const
{
    const auto& song = engine.getSong();
    const auto nameOf = [&song] (int track) {
        return track >= 0 && track < (int) song.mixer.size() ? juce::String::fromUTF8 (song.mixer[(std::size_t) track].name.c_str()) : juce::String();
    };
    const auto name = nameOf (t);

    if (part == Part::pan)
    {
        auto spec = ui::panSpec (name + " - " + tr ("Pan"));
        spec.pixelsForRange = 150.0;
        spec.target = AutomationTarget { AutomationTargetKind::mixerPan, t };
        return spec;
    }

    if (part == Part::stereo)
        return stereoSpec (name + " - " + tr ("Stereo separation"));

    if (part == Part::send)
    {
        auto spec = sendSpec (nameOf (selected) + juce::String::fromUTF8 (" \xe2\x86\x92 ") + name + " - "
                              + (t == 0 ? tr ("Level to Master") : engine.getSong().sendPre (selected, t) ? tr ("Send level (pre-fader)") : tr ("Send level")));
        spec.target = AutomationTarget { AutomationTargetKind::mixerSend, selected, t }; // automatable (issue #12)
        return spec;
    }

    auto spec = ui::faderSpec (name + " - " + tr ("Volume"), unityFader);
    spec.pixelsForRange = juce::jmax (60, faderRail (juce::jmax (0, t)).getHeight());
    spec.target = AutomationTarget { AutomationTargetKind::mixerVolume, t };
    return spec;
}

double MixerStrips::valueOf (Part part, int t) const
{
    const auto& song = engine.getSong();
    const auto& track = song.mixer[(std::size_t) t];

    if (part == Part::send)
    {
        return faderPosition (song.routeLevel (selected, t));
    }

    return part == Part::pan ? track.pan : part == Part::stereo ? track.stereo : track.volume;
}

void MixerStrips::setValue (Part part, int t, double value)
{
    if (t < 0 || t >= (int) engine.getSong().mixer.size())
        return;

    if (part == Part::send)
    {
        const auto from = selected;
        engine.editSong ([from, t, v = faderGain ((float) value)] (Song& s) { s.setRouteLevel (from, t, v); });
        return;
    }

    if (part == Part::stereo)
    {
        engine.editLevels ([t, v = (float) value] (Song& s) { s.mixer[(std::size_t) t].stereo = v; });
        return;
    }

    // during a drag of a selected strip, the other selected strips move by the same amount
    const auto delta = (float) value - groupGrab;
    const auto group = dragTrack == t && dragPart == part ? groupStart : std::vector<std::pair<int, float>> {};

    if (part == Part::pan)
        engine.editLevels ([t, v = (float) value, group, delta] (Song& s) {
            s.mixer[(std::size_t) t].pan = v;
            for (auto [other, start] : group)
                if (other < (int) s.mixer.size())
                    s.mixer[(std::size_t) other].pan = juce::jlimit (-1.0f, 1.0f, start + delta);
        });
    else if (part == Part::fader)
        engine.editLevels ([t, v = (float) value, group, delta] (Song& s) {
            s.mixer[(std::size_t) t].volume = v;
            for (auto [other, start] : group)
                if (other < (int) s.mixer.size())
                    s.mixer[(std::size_t) other].volume = juce::jlimit (0.0f, 1.0f, start + delta);
        });
}

void MixerStrips::setSelection (std::vector<int> tracks, int primary)
{
    const auto count = (int) engine.getSong().mixer.size();
    tracks.erase (std::remove_if (tracks.begin(), tracks.end(), [count] (int t) { return t < 0 || t >= count; }), tracks.end());
    primary = juce::jlimit (0, juce::jmax (0, count - 1), primary);
    tracks.push_back (primary);
    std::sort (tracks.begin(), tracks.end());
    tracks.erase (std::unique (tracks.begin(), tracks.end()), tracks.end());
    selection = tracks;
    const auto changed = primary != selected;
    selected = primary;
    keepSelectionShown();
    repaint();

    if (changed && onSelectionChanged)
        onSelectionChanged (selected);
}

bool MixerStrips::moveSelected (int delta)
{
    auto moved = selection;
    bool ok = false;
    engine.editSongStructural ([&] (Song& s) { ok = mixerops::moveTracks (s, moved, delta); });

    if (ok)
        setSelection (moved, juce::jlimit (1, (int) engine.getSong().mixer.size() - 1, selected + (delta < 0 ? -1 : 1)));

    return ok;
}

// FL's "Delete" on the selected mixer tracks (round 186c; REA FUN_012a5b60, FL 2026 checked by hand): an error while
// audio is being recorded; "Delete tracks|This will |delete| %d selected mixer track(s). Are you sure?" (Yes / No and
// "Remember my choice": a remembered Yes skips it, No is never remembered; Settings › General › Manage messages shows
// it again); then, when a Playlist audio / instrument track is linked to one of them, "Playlist tracks" with "Delete
// tracks and channels" / "Unlink tracks and channels" / "Cancel" (nothing remembered). One undo step.
void MixerStrips::deleteTracks (std::vector<int> tracks)
{
    const auto count = (int) engine.getSong().mixer.size();
    tracks.erase (std::remove_if (tracks.begin(), tracks.end(), [count] (int t) { return t < 1 || t >= count; }), tracks.end());
    if (tracks.empty())
        return;

    if (engine.isRecording() && ! engine.getArmedInserts().empty())
    {
        showFlMessage ({ tr ("Error"), tr ("It isn't possible to delete mixer tracks during audio recording."), { tr ("OK") }, false }, nullptr);
        return;
    }

    juce::Component::SafePointer<MixerStrips> safe (this);
    auto run = [safe, tracks] (mixerops::LinkedPlaylistTracks linked) {
        if (safe == nullptr)
            return;
        safe->engine.editSongStructural ([&tracks, linked] (Song& s) { mixerops::deleteTracks (s, tracks, linked, MixerStrips::keepLostLinks); });
        const auto first = juce::jlimit (0, (int) safe->engine.getSong().mixer.size() - 1, *std::min_element (tracks.begin(), tracks.end()));
        safe->setSelection ({ first }, first);
    };
    auto confirmed = [safe, tracks, run] {
        if (safe == nullptr)
            return;
        if (mixerops::playlistTracksLinkedTo (safe->engine.getSong(), tracks).empty())
        {
            run (mixerops::LinkedPlaylistTracks::unlink);
            return;
        }
        showFlMessage ({ tr ("Playlist tracks"),
                         tr ("One or more mixer tracks that will be deleted are connected to instrument/audio tracks.") + "\n"
                             + tr ("Do you want to unlink the tracks and channels or delete them?"),
                         { tr ("Delete tracks and channels"), tr ("Unlink tracks and channels"), tr ("Cancel") }, false },
                       [run] (int button, bool) {
                           if (button == 1)
                               run (mixerops::LinkedPlaylistTracks::deleteToo);
                           else if (button == 2)
                               run (mixerops::LinkedPlaylistTracks::unlink);
                       });
    };

    if (! messages::shown ("DeleteMixerTrack"))
    {
        confirmed();
        return;
    }
    // (FL marks "delete" in its accent colour: the |bars| of its own string)
    showFlMessage ({ tr ("Delete tracks"), tr ("This will |delete| {0} selected mixer track(s). Are you sure?", { juce::String ((int) tracks.size()) }),
                     { tr ("Yes"), tr ("No") }, true },
                   [confirmed] (int button, bool remember) {
                       if (button != 1)
                           return;
                       if (remember)
                           messages::setShown ("DeleteMixerTrack", false);
                       confirmed();
                   });
}

void MixerStrips::linkSelectedChannels (bool startingHere)
{
    const auto channels = engine.getSelectedChannels();
    engine.editSong ([channels, t = selected, startingHere] (Song& s) { mixerops::linkChannels (s, channels, t, startingHere); });
}

void MixerStrips::selectLinkedChannels()
{
    const auto channels = mixerops::channelsOn (engine.getSong(), selected);

    if (! channels.empty())
        engine.setChannelSelection (channels, channels.front());
}

void MixerStrips::soloExclusive (int t)
{
    if (t <= 0)
        return;

    // solo only this track; again on the only soloed track clears the solo
    engine.editSong ([t] (Song& s) {
        const auto alone = s.mixer[(std::size_t) t].solo
                           && std::none_of (s.mixer.begin() + 1, s.mixer.end(), [&] (const MixerTrack& m) { return &m != &s.mixer[(std::size_t) t] && m.solo; });
        for (std::size_t i = 1; i < s.mixer.size(); ++i)
            s.mixer[i].solo = ! alone && (int) i == t;
    });
}

void MixerStrips::routeSelection (int t, mixerops::RouteKind kind)
{
    using K = mixerops::RouteKind;
    int linked = 0;
    engine.editSong ([sources = selection, t, kind, &linked] (Song& s) {
        mixerops::routeSelectedTo (s, sources, t, kind);
        if (t >= 0 && t < (int) s.mixer.size())
            for (const auto& fx : s.mixer[(std::size_t) t].effects)
                linked += std::find (sources.begin(), sources.end(), fx.sidechain) != sources.end() ? 1 : 0;
    });
    if ((kind == K::sidechain || kind == K::sidechainOnly) && t > 0 && linked == 0)
        // (round 170h: in the hint bar, not a box — FL says nothing here; adding the compressor links it)
        ui::postHint ({ tr ("Sidechain") + ": " + tr ("The link was made; add a compressor to this insert to use it."), {} }, 6000);
}

// LYNX's pre-fader switch of a send (round 190aq; FL's sends are post-fader, the default): a ticked item in the send
// knob's and the route arrow's menus. The route to Master has none.
void MixerStrips::addPreFaderItem (juce::PopupMenu& menu, int from, int t)
{
    const auto& song = engine.getSong();
    if (t <= 0 || from <= 0 || ! song.routes (from, t))
        return;
    const auto pre = song.sendPre (from, t);
    menu.addItem (tr ("Pre-fader send (not moved by this track's fader)"), true, pre,
                  [this, from, t, pre] { engine.editSong ([from, t, pre] (Song& s) { s.setSendPre (from, t, ! pre); }); });
}

void MixerStrips::showSendMenu (int t)
{
    if (selected <= 0 || selected == t)
        return;
    buildSendMenu (t).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

juce::PopupMenu MixerStrips::buildSendMenu (int t)
{
    const auto& song = engine.getSong();
    const auto from = selected;

    const auto fromName = juce::String::fromUTF8 (song.mixer[(std::size_t) from].name.c_str());
    juce::PopupMenu menu;
    const auto sources = (int) std::count_if (selection.begin(), selection.end(), [t] (int x) { return x > 0 && x != t; });
    menu.addSectionHeader ((sources > 1 ? tr ("{0} selected tracks", { juce::String (sources) }) : fromName)
                           + juce::String::fromUTF8 (" \xe2\x86\x92 ") + juce::String::fromUTF8 (song.mixer[(std::size_t) t].name.c_str()));
    // FL's route arrow menu (round 168s, issue #32 [SHOT] r168s c_f10): route / route only / sidechain / sidechain only
    const auto on = song.routes (from, t);
    const auto can = on || song.canRoute (from, t);
    using K = mixerops::RouteKind;
    // FL (round 170b, REA FUN_012aca70): every item acts on all the selected tracks, not only the current one
    auto route = [this, t] (K kind) { routeSelection (t, kind); };
    menu.addItem (tr ("Route to this track"), can && ! on, false, [route] { route (K::route); });
    menu.addItem (tr ("Route to this track only"), can, false, [route] { route (K::routeOnly); });
    menu.addSeparator();
    menu.addItem (tr ("Sidechain to this track"), t > 0 && can, false, [route] { route (K::sidechain); });
    menu.addItem (tr ("Sidechain to this track only"), t > 0 && can, false, [route] { route (K::sidechainOnly); });
    if (on)
    {
        menu.addSeparator();
        addPreFaderItem (menu, from, t);
        menu.addItem (t == 0 ? tr ("Remove the route to Master") : tr ("Remove this send"),
                      [this, from, t] { engine.editSong ([from, t] (Song& s) { s.setRoute (from, t, false); }); });
    }
    return menu;
}

void MixerStrips::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (const auto i = vcaAt (e.x); i >= 0 && ! ui::wheelScrolling() && vcaPartAt (i, e.getPosition()) == Part::fader)
    {
        if (const auto v = ui::wheelValue (engine.getSong().vcas[(std::size_t) i].level, e, wheel, vcaSpec (i)))
        {
            setVcaLevel (i, *v);
            ui::flashValueTip (*this, faderRail (vcaStrip (i)), vcaSpec (i).text (*v));
        }
        return;
    }

    const auto t = trackAt (e.x);

    if (t > 0 && t < (int) engine.getSong().mixer.size() && e.mods.isShiftDown())
        if (const auto part = partAt (t, e.getPosition()); part == Part::body || part == Part::header)
        {
            if (! std::binary_search (selection.begin(), selection.end(), t))
                setSelection ({ t }, t);
            moveSelected ((wheel.deltaY != 0.0f ? wheel.deltaY : -wheel.deltaX) > 0.0f ? -1 : 1);
            return;
        }

    // (a spin that is scrolling the strips goes on scrolling over the faders and knobs, round 169c)
    if (t >= 0 && t < (int) engine.getSong().mixer.size() && ! ui::wheelScrolling())
        if (const auto part = partAt (t, e.getPosition()); part == Part::pan || part == Part::fader || part == Part::stereo || part == Part::send)
        {
            if (const auto v = ui::wheelValue (valueOf (part, t), e, wheel, specFor (part, t)))
            {
                setValue (part, t, *v);
                showTip (part, t, true);
            }
            return;
        }

    ui::noteWheelScroll(); // the wheel scrolls the strips (the viewport)
    juce::Component::mouseWheelMove (e, wheel);
}

ui::Hint MixerStrips::hintAt (juce::Point<int> p)
{
    if (dragVca >= 0)
        return vcaHint (dragVca, p);
    if (const auto i = vcaAt (p.x); i >= 0 && dragPart == Part::none)
        return vcaHint (i, p);
    if (showLoudness && dragPart == Part::none && loudnessArea().contains (p))
    {
        ui::Hint h;
        h.hasMenu = true;
        if (loudnessResetArea().contains (p))
            h.text = tr ("Start the loudness measurement again");
        else
        {
            h.text = tr ("Loudness of the Master: S = short-term (3 s), I = integrated since the last reset (LUFS), TP = true peak (dBTP)");
            h.value = "S " + juce::String (formatLoudness (loudnessShown.shortTerm)) + "  I " + juce::String (formatLoudness (loudnessShown.integrated))
                      + "  TP " + juce::String (formatLoudness (loudnessShown.truePeak));
        }
        return h;
    }
    const auto active = dragPart == Part::pan || dragPart == Part::fader || dragPart == Part::stereo || dragPart == Part::send;
    const auto t = active ? dragTrack : trackAt (p.x);
    const auto& song = engine.getSong();
    ui::Hint h;

    if (t < 0 || t >= (int) song.mixer.size())
    {
        if (t == (int) song.mixer.size())
            h.text = tr ("Add a mixer insert");
        return h;
    }

    const auto part = active ? dragPart : partAt (t, p);
    const auto name = juce::String::fromUTF8 (song.mixer[(std::size_t) t].name.c_str());
    const auto gesture = ui::valueGestures();

    // the channels that play into this insert (FL shows them by the insert; LYNX in the hint)
    auto feeds = [&song, t, tracks = (int) song.mixer.size()] {
        juce::StringArray names;
        for (const auto& c : song.channels)
            if (juce::jlimit (0, tracks - 1, c.mixerTrack) == t)
                names.add (juce::String::fromUTF8 (c.name.c_str()));
        if (names.isEmpty())
            return juce::String();
        const auto shown = names.size() > 6 ? names[0] + ", " + names[1] + ", " + names[2] + " +" + juce::String (names.size() - 3)
                                            : names.joinIntoString (", ");
        return "   " + tr ("Channels: {0}", { shown });
    };
    // FL's "Track latency compensation" label (REA round 165f-mixer): the manual latency and the bypass, when set
    auto latency = [&song, t] {
        const auto& track = song.mixer[(std::size_t) t];
        juce::String text;
        if (track.latencyMs != 0.0f)
            text << "   " << tr ("Track latency: {0}", { (track.latencyMs > 0.0f ? "+" : "") + juce::String (track.latencyMs, 2) + " ms" });
        if (track.latencyBypass == LatencyBypass::always)
            text << "   " << tr ("Bypass track latency compensation") << ": " << tr ("Always");
        else if (track.latencyBypass == LatencyBypass::whenRecordingOrStopped)
            text << "   " << tr ("Bypass track latency compensation") << ": " << tr ("When recording or not playing");
        return text;
    };

    switch (part)
    {
        case Part::fader:
            if (const auto note = vcaNote (t); note.isNotEmpty())
            {
                h = ui::valueHint (specFor (part, t), valueOf (part, t), gesture);
                h.text << "   " << note;
                return h;
            }
            return ui::valueHint (specFor (part, t), valueOf (part, t), gesture);
        case Part::pan:
        case Part::stereo:
        case Part::send:
            return ui::valueHint (specFor (part, t), valueOf (part, t), gesture);
        case Part::mute: h.text = name + " - " + tr ("Click: mute.  Right-click or Ctrl+click: solo"); break;
        case Part::arm: h.text = name + " - " + mixerinput::armHint (engine, t); h.hasMenu = true; break;
        case Part::input: h.text = name + " - " + tr ("Input level of this insert's audio input  [LMB] choose the input"); h.hasMenu = true; break;
        case Part::solo: h.text = name + " - " + tr ("Solo"); break;
        case Part::meter: h.text = name + " - " + tr ("Peak meter (left/right) with peak hold  [LMB] selects the track and clears the clip light"); break;
        case Part::route: h.text = name + " - " + tr ("Route the selected inserts to this one (drag after enabling: level). Ctrl+click: this track only, Shift: sidechain, Ctrl+Shift: sidechain only, right-click: menu"); h.hasMenu = true; break;
        case Part::add: h.text = tr ("Add a mixer insert"); break;
        case Part::fold:
            h.text = name + " - " + (mixerops::isFolded (song, t) ? tr ("Unfold this group  [Alt]+[LMB] all groups")
                                                                : tr ("Fold this group to its bus strip, or to a slim tab  [Alt]+[LMB] all groups"));
            break;
        case Part::tab:
            h.text = tr ("Folded group: {0} ({1} inserts)  [LMB] unfold", { name, juce::String ((int) mixerops::groupOf (song, t).size()) });
            break;
        case Part::header:
            h.text = name + " - " + tr ("[LMB] selects  [Ctrl]+[LMB] or drag across: range  [Ctrl]+[Shift]+[LMB] adds or removes  [LMB] drag sideways reorders  [RMB] insert menu") + feeds() + latency();
            h.hasMenu = true;
            if (engine.insertPlaysStandIn (t))
                h.text = name + " - " + tr ("Stand-in: a plugin of this insert is not installed on this PC, a render of its sound plays (the plugins' settings are kept)");
            else if (engine.insertPluginOff (t)) // round 190ap
                h.text = name + " - " + tr ("A plugin of this insert is switched off: the project was opened without third-party plugins (its settings are kept; File › Switch plugins back on...)");
            break;
        case Part::body:
        case Part::none:
            h.text = name + " - " + tr ("Click selects the insert (Ctrl+click or drag across: range, Ctrl+Shift: add or remove), right-click opens its menu; Alt+Left/Right moves the selection") + feeds() + latency();
            h.hasMenu = true;
            break;
    }

    return h;
}

void MixerStrips::mouseUp (const juce::MouseEvent&)
{
    railRepeat.stopTimer();
    railStrip = railNone;

    if (headerDragged)
        setMouseCursor (juce::MouseCursor::NormalCursor);

    if (dragPart != Part::none || headerDragged || dragVca >= 0)
        repaint();
    dragVca = -1;

    headerDragTrack = -1;
    headerDragged = false;
    selectAnchor = -1;
    selectBase.clear();
    dragPart = Part::none;
    dragTrack = -1;
    drag.end();
    ui::hideValueTip();
    ui::clearPostedHint();
}

void MixerStrips::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (const auto i = vcaAt (e.x); i >= 0)
    {
        if (vcaPartAt (i, e.getPosition()) == Part::fader)
            setVcaLevel (i, unityFader);
        return;
    }

    const auto t = trackAt (e.x);

    if (t < 0 || t >= (int) engine.getSong().mixer.size())
        return;

    const auto part = partAt (t, e.getPosition());

    if (part == Part::fader || part == Part::pan || part == Part::stereo || part == Part::send)
    {
        setValue (part, t, specFor (part, t).defaultValue);
        showTip (part, t, true);
    }
}

void MixerStrips::keepSelectionShown()
{
    if (! showUsedOnly)
        return;
    const auto wasHidden = std::any_of (selection.begin(), selection.end(), [this] (int t) { return revealed.count (t) == 0 && isHidden (t); })
                           || (revealed.count (selected) == 0 && isHidden (selected));
    if (wasHidden) // (selected from the Channel Rack, a menu or the keyboard: its strip comes back)
        updateSize();
}

void MixerStrips::selectTrack (int track)
{
    selected = juce::jlimit (0, juce::jmax (0, (int) engine.getSong().mixer.size() - 1), track);
    selection = { selected };
    keepSelectionShown();
    repaint();

    if (onSelectionChanged)
        onSelectionChanged (selected);
}

namespace
{
// The 3 knobs of each band of the track's built-in EQ (gain, frequency, width).
class TrackEqEditor final : public juce::Component
{
public:
    TrackEqEditor (AudioEngine& e, int t)
        : engine (e),
          track (t)
    {
        const juce::String bands[] = { tr ("Low"), tr ("Mid"), tr ("High") };
        const auto& eq = engine.getSong().mixer[(std::size_t) track].eq;

        for (int b = 0; b < 3; ++b)
        {
            titles[(std::size_t) b].setText (bands[b], juce::dontSendNotification);
            titles[(std::size_t) b].setJustificationType (juce::Justification::centred);
            addAndMakeVisible (titles[(std::size_t) b]);

            auto& gain = knobs[(std::size_t) b][0];
            setup (gain, -18.0, 18.0, 0.1, " dB", eq.gainDb[(std::size_t) b]);
            gain.setDoubleClickReturnValue (true, 0.0);

            auto& hz = knobs[(std::size_t) b][1];
            setup (hz, 10.0, 16000.0, 1.0, " Hz", eq.hz[(std::size_t) b]);
            hz.setSkewFactorFromMidPoint (1000.0);

            auto& width = knobs[(std::size_t) b][2];
            setup (width, 0.0, 1.0, 0.01, "", eq.width[(std::size_t) b]);
            width.setTooltip (b == 1 ? tr ("Peak width (Q)") : tr ("Shelf slope"));
        }

        setSize (3 * 90, 3 * 74 + 24);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto header = area.removeFromTop (20);

        for (int b = 0; b < 3; ++b)
        {
            titles[(std::size_t) b].setBounds (header.removeFromLeft (90));
            auto column = area.removeFromLeft (90);

            for (auto& k : knobs[(std::size_t) b])
                k.setBounds (column.removeFromTop (74));
        }
    }

private:
    void setup (juce::Slider& s, double lo, double hi, double step, const char* suffix, float value)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 16);
        s.setRange (lo, hi, step);
        s.setTextValueSuffix (suffix);
        s.setValue (value, juce::dontSendNotification);
        s.onValueChange = [this] { apply(); };
        addAndMakeVisible (s);
    }

    void apply()
    {
        TrackEq eq;

        for (std::size_t b = 0; b < 3; ++b)
        {
            eq.gainDb[b] = (float) knobs[b][0].getValue();
            eq.hz[b] = (float) knobs[b][1].getValue();
            eq.width[b] = (float) knobs[b][2].getValue();
        }

        engine.editSong ([t = track, eq] (Song& s) {
            if (t < (int) s.mixer.size())
                s.mixer[(std::size_t) t].eq = eq;
        });
    }

    AudioEngine& engine;
    int track;
    std::array<juce::Label, 3> titles;
    std::array<std::array<juce::Slider, 3>, 3> knobs; // [band][gain, frequency, width]
};
} // namespace

void MixerStrips::showTrackEq (int t)
{
    auto editor = std::make_unique<TrackEqEditor> (engine, t);
    juce::CallOutBox::launchAsynchronously (std::move (editor), getScreenBounds().withSize (10, 10).translated (getMouseXYRelative().x, getMouseXYRelative().y), nullptr);
}

namespace
{
juce::File mixerStatesFolder()
{
    return lynx::platform::documentsFolder().getChildFile ("LYNX/Mixer states");
}

std::unique_ptr<juce::FileChooser>& mixerStateChooser()
{
    static std::unique_ptr<juce::FileChooser> chooser;
    return chooser;
}

// --- FL's track latency menu (REA round 165f-mixer: DelayMenu, FUN_012ae090 / FUN_012ad520 / FUN_012acd30) ---------

double sampleRateOf (const AudioEngine& engine)
{
    return juce::jmax (1.0, engine.getTransport().sampleRate.load());
}

// The track's latency in the current PDC plan (its output: inputs + effects + manual), in ms
double trackLatencyMs (const AudioEngine& engine, int t)
{
    const auto& plan = engine.getPdcPlan();
    return t >= 0 && t < (int) plan.outLat.size() ? 1000.0 * plan.outLat[(std::size_t) t] / sampleRateOf (engine) : 0.0;
}

// FL's label: "(none)" or "<ms> ms / <samples> smp" of the manual latency
juce::String manualLatencyText (const AudioEngine& engine, const MixerTrack& track)
{
    if (track.latencyMs == 0.0f)
        return tr ("(none)");
    const auto samples = manualLatencySamples (track, sampleRateOf (engine));
    return (track.latencyMs > 0.0f ? "+" : "") + juce::String (track.latencyMs, 2) + " ms / " + (samples > 0 ? "+" : "") + juce::String (samples) + " smp";
}

void setTrackLatency (AudioEngine& engine, int t, double ms)
{
    if (t < 0 || t >= (int) engine.getSong().mixer.size() || ! std::isfinite (ms))
        return;
    const auto v = (float) juce::jlimit ((double) -maxTrackLatencyMs, (double) maxTrackLatencyMs, ms);
    engine.editSong ([t, v] (Song& s) { s.mixer[(std::size_t) t].latencyMs = v; });
}

// "Set in ms / samples / beats": FL's input box, the value in that unit (FUN_012ad520)
void askTrackLatency (AudioEngine& engine, int t, int unit)
{
    const auto& track = engine.getSong().mixer[(std::size_t) t];
    const auto rate = sampleRateOf (engine);
    const auto bpm = juce::jmax (1.0, engine.getSong().bpm);
    const juce::String units[] = { tr ("Latency (ms)"), tr ("Latency (samples)"), tr ("Latency (beats)") };
    const auto value = unit == 0 ? juce::String (track.latencyMs, 2)
                     : unit == 1 ? juce::String (manualLatencySamples (track, rate))
                                 : juce::String (track.latencyMs * bpm / 60000.0, 6);
    auto* w = new juce::AlertWindow (tr ("Track latency"),
                                     tr ("Positive: the other tracks wait for this one (it plays earlier). Negative: this track is delayed."),
                                     juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("v", value, units[juce::jlimit (0, 2, unit)]);
    w->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([w, &engine, t, unit, rate, bpm] (int result) {
        if (result != 1)
            return;
        const auto text = w->getTextEditorContents ("v").trim().replaceCharacter (',', '.');
        if (! text.containsAnyOf ("0123456789"))
            return;
        const auto v = text.getDoubleValue();
        setTrackLatency (engine, t, unit == 0 ? v : unit == 1 ? v * 1000.0 / rate : v * 60000.0 / bpm);
    }), true);
}

juce::PopupMenu trackLatencyMenu (AudioEngine& engine, int t)
{
    const auto& song = engine.getSong();
    const auto& track = song.mixer[(std::size_t) t];
    juce::PopupMenu menu;
    // FL's hint "^Track latency": "<x>ms (auto) <y>ms (manual)"
    // auto = the plan's latency without the manual part, in samples (no ms rounding left over when no plugin has any)
    const auto total = trackLatencyMs (engine, t);
    const auto rate = sampleRateOf (engine);
    const auto autoMs = total - 1000.0 * lynx::manualLatencySamples (track, rate) / rate;
    menu.addSectionHeader (tr ("Latency {0} ms (auto {1} ms, manual {2} ms)",
                               { juce::String (total, 2), juce::String (std::abs (autoMs) < 0.005 ? 0.0 : autoMs, 2), juce::String (track.latencyMs, 2) }));
    menu.addItem (tr ("Reset"), track.latencyMs != 0.0f, false, [&engine, t] { setTrackLatency (engine, t, 0.0); });
    menu.addItem (tr ("Set in ms..."), [&engine, t] { askTrackLatency (engine, t, 0); });
    menu.addItem (tr ("Set in samples..."), [&engine, t] { askTrackLatency (engine, t, 1); });
    menu.addItem (tr ("Set in beats..."), [&engine, t] { askTrackLatency (engine, t, 2); });
    {
        // FL's "Set from": the tracks whose latency is not zero; this track's total becomes theirs (FUN_012acd30)
        juce::PopupMenu from;
        for (int o = 0; o < (int) song.mixer.size(); ++o)
        {
            const auto ms = trackLatencyMs (engine, o);
            if (o == t || std::abs (ms) < 0.005)
                continue;
            const auto name = o == 0 ? tr ("Master") : juce::String (o) + " " + juce::String::fromUTF8 (song.mixer[(std::size_t) o].name.c_str());
            from.addItem (name + "  (" + juce::String (ms, 2) + " ms)", [&engine, t, o] {
                const auto manual = (double) engine.getSong().mixer[(std::size_t) t].latencyMs + trackLatencyMs (engine, o) - trackLatencyMs (engine, t);
                if (manual < 0.0)
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Track latency"),
                                                            tr ("The delay introduced by this mixer track is higher than the delay introduced by the source "
                                                                "mixer track. Set a make-up latency on the source track instead."));
                else
                    setTrackLatency (engine, t, manual);
            });
        }
        menu.addSubMenu (tr ("Set from"), from, from.getNumItems() > 0);
    }
    menu.addSeparator();
    menu.addItem (tr ("Compensate for master latency"), track.outputChannel >= 0, track.compensateMasterLatency, [&engine, t] {
        engine.editSong ([t] (Song& s) { s.mixer[(std::size_t) t].compensateMasterLatency = ! s.mixer[(std::size_t) t].compensateMasterLatency; });
    });
    if (t > 0)
    {
        menu.addSectionHeader (tr ("Bypass track latency compensation"));
        const std::pair<const char*, LatencyBypass> modes[] = { { TR_NOOP ("Never"), LatencyBypass::never },
                                                                 { TR_NOOP ("Always"), LatencyBypass::always },
                                                                 { TR_NOOP ("When recording or not playing"), LatencyBypass::whenRecordingOrStopped } };
        for (const auto& [label, mode] : modes)
            menu.addItem (tr (label), true, track.latencyBypass == mode, [&engine, t, mode = mode] {
                engine.editSong ([t, mode] (Song& s) { s.mixer[(std::size_t) t].latencyBypass = mode; });
            });
    }
    return menu;
}
} // namespace

bool loadMixerTrackState (AudioEngine& engine, int track, const juce::File& file, bool asNewInsert)
{
    if (track < 0 || track >= (int) engine.getSong().mixer.size())
        return false;
    if (asNewInsert)
        return mixerpresets::insertTrack (engine, juce::jmax (1, track + 1), file) > 0;
    return mixerpresets::loadOnTracks (engine, { track }, file);
}

namespace mixerpresets
{
juce::Array<juce::File> folders()
{
    // FL's browser folder 0x3f8 (REA round 167q, FUN_009db5b0): "Presets\Mixer presets" of the user data folder and
    // "Data\Patches\Mixer presets" of the installation
    static const auto fl = findFlStudio();
    juce::Array<juce::File> out;
    if (fl.userData.isDirectory())
        out.add (fl.userData.getChildFile ("Presets").getChildFile ("Mixer presets"));
    if (fl.installDir.isDirectory())
        out.add (fl.installDir.getChildFile ("Data").getChildFile ("Patches").getChildFile ("Mixer presets"));
    out.add (mixerStatesFolder());
    out.removeIf ([] (const juce::File& f) { return ! f.isDirectory(); });
    return out;
}

juce::Array<juce::File> all()
{
    juce::Array<juce::File> files;
    for (const auto& folder : folders())
        for (const auto& f : folder.findChildFiles (juce::File::findFiles, true, juce::String ("*.fst;*") + ProjectFile::mixerTrackStateExtension))
            files.add (f);
    std::stable_sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b) {
        return a.getFileNameWithoutExtension().compareNatural (b.getFileNameWithoutExtension()) < 0;
    });
    return files;
}

static std::vector<std::uint8_t> bytesOf (const juce::File& file)
{
    juce::MemoryBlock data;
    if (file.getSize() > 64 * 1024 * 1024 || ! file.loadFileAsData (data))
        return {};
    const auto* bytes = static_cast<const std::uint8_t*> (data.getData());
    return { bytes, bytes + data.getSize() };
}

bool isMixerPreset (const juce::File& file)
{
    if (file.hasFileExtension (ProjectFile::mixerTrackStateExtension))
        return true;
    if (! file.hasFileExtension ("fst"))
        return false;
    for (auto p = file.getParentDirectory(); p != juce::File() && p.getParentDirectory() != p; p = p.getParentDirectory())
        if (p.getFileName().equalsIgnoreCase ("Mixer presets"))
            return true;
    const auto bytes = bytesOf (file);
    return flp::looksLikeMixerPreset (bytes);
}

std::optional<MixerTrack> read (const juce::File& file)
{
    if (file.hasFileExtension ("fst"))
    {
        FlpImportOptions options;
        options.roots = findFlStudioRoots();
        options.fileExists = [] (const std::string& path) { return platform::localFile (path).existsAsFile(); };
        options.readFile = [] (const std::string& path) { return bytesOf (platform::localFile (path)); };
        return mixerTrackFromFlPreset (bytesOf (file), options);
    }
    juce::String error;
    auto state = ProjectFile::mixerTrackStateFromJson (file.loadFileAsString(), error);
    if (state && state->name.empty())
        state->name = file.getFileNameWithoutExtension().toStdString();
    return state;
}

bool loadOnTracks (AudioEngine& engine, const std::vector<int>& tracks, const juce::File& file)
{
    const auto state = read (file);
    if (! state)
        return false;
    // FL's mixer presets restore the track's name and colour (WhatsNew); LYNX's states keep the track's own
    const auto takeName = file.hasFileExtension ("fst") && ! state->name.empty();
    bool any = false;
    engine.editSong ([&] (Song& s) {
        for (const auto t : tracks)
            if (t >= 0 && t < (int) s.mixer.size())
            {
                ProjectFile::applyMixerTrackState (s.mixer[(std::size_t) t], *state, takeName);
                any = true;
            }
    });
    return any;
}

int insertTrack (AudioEngine& engine, int at, const juce::File& file)
{
    auto state = read (file);
    if (! state)
        return -1;
    if (state->name.empty())
        state->name = file.getFileNameWithoutExtension().toStdString();
    at = juce::jlimit (1, (int) engine.getSong().mixer.size(), at);
    bool ok = false;
    engine.editSong ([&] (Song& s) {
        ok = mixerops::insertTrack (s, at);
        if (ok)
            ProjectFile::applyMixerTrackState (s.mixer[(std::size_t) at], *state, true);
    });
    return ok ? at : -1;
}
} // namespace mixerpresets

bool saveMixerTrackState (AudioEngine& engine, int track, const juce::File& file)
{
    if (track < 0 || track >= (int) engine.getSong().mixer.size())
        return false;
    engine.capturePluginStates(); // the plugins' current settings go into the state
    file.getParentDirectory().createDirectory();
    return file.replaceWithText (ProjectFile::mixerTrackStateToJson (engine.getSong().mixer[(std::size_t) track]));
}

// FL's "Auto-create audio clip" (round 167a [SHOT s28–s37]): the rendered files go on the Playlist, one undo step
static void placeRenderedFiles (AudioEngine& engine, const std::vector<juce::File>& files)
{
    std::vector<mixerops::RenderedFile> list;
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    for (const auto& f : files)
        if (std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f)); reader != nullptr)
            list.push_back ({ f.getFullPathName().toStdString(), (double) reader->lengthInSamples, reader->sampleRate });
    if (! list.empty())
        engine.editSong ([list] (Song& s) { mixerops::placeRenderedFiles (s, list); });
}

void MixerStrips::askInsertMultiple (int at)
{
    auto* window = new juce::AlertWindow (tr ("Insert multiple"), tr ("How many blank inserts?"), juce::MessageBoxIconType::NoIcon);
    // FL 2026's box starts at 10, already selected; Ctrl+A selects it again (round 167a [SHOT s13–s15])
    window->addTextEditor ("count", "10");
    if (auto* field = window->getTextEditor ("count"))
        field->setInputRestrictions (4, "0123456789");
    window->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<MixerStrips> safe (this);
    window->enterModalState (true, juce::ModalCallbackFunction::create ([safe, at, window] (int result) {
                                 if (safe == nullptr || result != 1)
                                     return;
                                 const auto n = window->getTextEditorContents ("count").getIntValue();
                                 const auto room = mixerops::roomForInserts (safe->engine.getSong());
                                 if (n > room) // FL 2026 shows "You can add a maximum of 375 tracks." (round 166d [SHOT]); nothing is added then
                                 {
                                     juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Insert multiple"),
                                                                             tr ("You can add a maximum of {0} inserts.", { juce::String (room) }));
                                     return;
                                 }
                                 if (n > 0)
                                 {
                                     const auto first = juce::jlimit (1, (int) safe->engine.getSong().mixer.size(), at);
                                     safe->engine.editSong ([first, n] (Song& s) { mixerops::insertTracks (s, first, n); });
                                     for (int i = 0; i < n; ++i)
                                         safe->revealed.insert (first + i); // shown even while unused
                                     safe->updateSize();
                                     safe->selectTrack (first);
                                 }
                             }),
                             true);
    if (auto* field = window->getTextEditor ("count"))
    {
        field->grabKeyboardFocus();
        field->selectAll();
    }
}

void MixerStrips::showMenu (int t)
{
    buildMenu (t).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

bool MixerStrips::runMenuItem (const juce::String& caption)
{
    auto menu = buildMenu (getSelectedTrack());
    for (juce::PopupMenu::MenuItemIterator it (menu, true); it.next();)
    {
        const auto& item = it.getItem();
        if (item.isEnabled && item.action != nullptr && item.subMenu == nullptr
            && item.text.upToFirstOccurrenceOf ("\t", false, false).trim() == caption)
        {
            const auto action = item.action; // (the menu goes away first)
            menu = {};
            action();
            return true;
        }
    }
    return false;
}

juce::PopupMenu MixerStrips::buildMenu (int t)
{
    const auto& song = engine.getSong();
    const auto channel = engine.getSelectedChannel();
    juce::PopupMenu menu;
    if (t < 0 || t >= (int) song.mixer.size())
        return menu;
    // the tracks the "selected track(s)" items act on: the selection when the clicked track is in it
    const auto targets = std::find (selection.begin(), selection.end(), t) != selection.end() ? selection : std::vector<int> { t };
    const auto trackName = juce::String::fromUTF8 (song.mixer[(std::size_t) t].name.c_str());

    auto rename = [this, t] {
        if (t >= (int) engine.getSong().mixer.size())
            return;
        const auto& track = engine.getSong().mixer[(std::size_t) t];
        showNameAndColourDialog (tr ("Rename track"), juce::String::fromUTF8 (track.name.c_str()), track.colour,
                                 [this, t] (const juce::String& name, std::uint32_t colour) {
                                     if (t < (int) engine.getSong().mixer.size())
                                         engine.editSong ([t, n = name.trim().toStdString(), colour] (Song& s) {
                                             if (! n.empty())
                                                 s.mixer[(std::size_t) t].name = n;
                                             s.mixer[(std::size_t) t].colour = colour;
                                         });
                                 });
    };

    // the title band in the insert's colour (FL tints the whole menu); a double-click on it renames (issue #12)
    // FL: "Mixer tracks (N selected)" when the menu acts on several tracks
    const auto several = targets.size() > 1;
    ui::addItemHeader (menu, several ? juce::String() : t == 0 ? tr ("Master") : tr ("Mixer track {0}", { juce::String (t) }),
                       several ? tr ("Mixer tracks ({0} selected)", { juce::String ((int) targets.size()) }) : trackName,
                       song.mixer[(std::size_t) t].colour != 0 ? juce::Colour (song.mixer[(std::size_t) t].colour) : juce::Colour(), rename);

    // FL's first section: name and colour (REA_rodada115)
    menu.addItem (tr ("Rename and color...") + "\tF2", rename);
    menu.addItem (tr ("Change color..."), [this, t] { // (FL: the colour selector at once)
        const auto current = engine.getSong().mixer[(std::size_t) t].colour;
        const auto mouse = juce::Desktop::getMousePosition();
        const auto set = [this, t] (std::uint32_t colour) {
            if (t < (int) engine.getSong().mixer.size())
                engine.editSong ([t, colour] (Song& s) { s.mixer[(std::size_t) t].colour = colour; });
        };
        ui::showColourPickerPopup ({ mouse.x, mouse.y, 1, 1 }, tr ("Change color..."), current != 0 ? juce::Colour (current) : juce::Colour (lynxPalette()[0]), {},
                                   [set] (juce::Colour c) { set (c.getARGB()); }, {}, [set] { set (0); });
    });
    menu.addItem (tr ("Random color"), [this, t] {
        const auto& palette = lynxPalette();
        const auto colour = palette[(std::size_t) juce::Random::getSystemRandom().nextInt ((int) palette.size())];
        engine.editSong ([t, colour] (Song& s) { s.mixer[(std::size_t) t].colour = colour; });
    });
    // FL's "Gradient color..." (SetGradientColorMenuClick, REA_rodada177a §5): greyed below two selected tracks; the
    // Gradient editor starts from the first and the last selected track's colours
    menu.addItem (tr ("Gradient color..."), targets.size() > 1, false, [this, targets] {
        auto sorted = targets;
        std::sort (sorted.begin(), sorted.end());
        const auto& mixer = engine.getSong().mixer;
        const auto colourOf = [&mixer] (int track) {
            const auto c = track >= 0 && track < (int) mixer.size() ? mixer[(std::size_t) track].colour : 0u;
            return c != 0 ? juce::Colour (c) : juce::Colour (lynxPalette()[0]);
        };
        showGradientColourDialog (colourOf (sorted.front()), colourOf (sorted.back()), (int) sorted.size(),
                                  [this, sorted] (juce::Colour first, juce::Colour last) {
                                      engine.editSong ([sorted, a = first.getARGB(), b = last.getARGB()] (Song& s) {
                                          mixerops::gradientColour (s, sorted, a, b);
                                      });
                                  });
    });
    // FL's "Change icon..." (SetIconMenuClick, REA round 168h: every selected track gets it); LYNX's own icon set
    menu.addItem (tr ("Change icon..."), [this, t, targets = std::find (selection.begin(), selection.end(), t) != selection.end() ? selection : std::vector<int> { t }] {
        ui::showTrackIconPicker (juce::Rectangle<int> (juce::Desktop::getMousePosition().x, juce::Desktop::getMousePosition().y, 1, 1), engine.getSong().mixer[(std::size_t) t].icon, [this, targets] (int icon) {
            engine.editSong ([targets, icon] (Song& s) {
                for (auto track : targets)
                    if (track >= 0 && track < (int) s.mixer.size())
                        s.mixer[(std::size_t) track].icon = icon;
            });
        });
    });
    // FL's "Assign to new Audio track(s)" / "Unassign from Audio track(s)" (AssignAudiotrackMenuClick, REA_rodada177a §6)
    {
        const auto linked = [&song] (int insert) {
            return std::any_of (song.playlistTracks.begin(), song.playlistTracks.end(), [insert] (const PlaylistTrack& pt) {
                return pt.mode == PlaylistTrackMode::audio && pt.mixerTrack == insert;
            });
        };
        const auto anyLinked = std::any_of (targets.begin(), targets.end(), [&] (int x) { return x > 0 && linked (x); });
        const auto anyFree = std::any_of (targets.begin(), targets.end(), [&] (int x) { return x > 0 && ! linked (x); });
        menu.addItem (tr ("Assign to new Audio track(s)"), anyFree, false,
                      [this, targets] { engine.editSong ([targets] (Song& s) { mixerops::assignNewAudioTracks (s, targets); }); });
        menu.addItem (tr ("Unassign from Audio track(s)"), anyLinked, false,
                      [this, targets] { engine.editSong ([targets] (Song& s) { mixerops::unassignAudioTracks (s, targets); }); });
    }
    menu.addSeparator();

    // FL's "File" submenu (REA round 167q, TFXForm's FileMenu): open/save a state, Browse states and the "States" list
    // of FL's mixer presets (FL's own .fst and the user's) and LYNX's .lynxtrack states, loaded onto every selected
    // track (FUN_012a7440)
    {
        const auto targets = std::find (selection.begin(), selection.end(), t) != selection.end() ? selection : std::vector<int> { t };
        juce::PopupMenu file;
        file.addItem (tr ("Open mixer track state..."), [this, targets] {
            auto& chooser = mixerStateChooser();
            const auto start = mixerpresets::folders().isEmpty() ? mixerStatesFolder() : mixerpresets::folders()[0];
            chooser = lynx::ui::makeFileChooser (tr ("Open mixer track state"), start,
                                                           juce::String ("*.fst;*") + ProjectFile::mixerTrackStateExtension);
            juce::Component::SafePointer<MixerStrips> safe (this);
            lynx::ui::launchChooser (chooser, juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe, targets] (const juce::FileChooser& fc) {
                if (safe != nullptr && fc.getResult().existsAsFile() && ! mixerpresets::loadOnTracks (safe->engine, targets, fc.getResult()))
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Open mixer track state"),
                                                            tr ("This file is not a mixer track state LYNX SHARE can read."));
            });
        });
        file.addItem (tr ("Save mixer track state as...") + "\tCtrl+Shift+S", [this, t, trackName] {
            auto& chooser = mixerStateChooser();
            const auto name = juce::File::createLegalFileName (trackName.isNotEmpty() ? trackName : tr ("Mixer track {0}", { juce::String (t) }));
            chooser = lynx::ui::makeFileChooser (tr ("Save mixer track state"), mixerStatesFolder().getChildFile (name + ProjectFile::mixerTrackStateExtension),
                                                           juce::String ("*") + ProjectFile::mixerTrackStateExtension);
            juce::Component::SafePointer<MixerStrips> safe (this);
            mixerStatesFolder().createDirectory();
            lynx::ui::launchChooser (chooser, juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [safe, t] (const juce::FileChooser& fc) {
                if (safe != nullptr && fc.getResult() != juce::File())
                    saveMixerTrackState (safe->engine, t, fc.getResult().withFileExtension (ProjectFile::mixerTrackStateExtension));
            });
        });
        file.addItem (tr ("Browse states"), [] {
            mixerStatesFolder().createDirectory();
            mixerStatesFolder().startAsProcess();
        });
        const auto states = mixerpresets::all();
        if (! states.isEmpty())
        {
            file.addSectionHeader (tr ("States"));
            for (const auto& f : states)
                file.addItem (f.getFileNameWithoutExtension(), [this, targets, f] { mixerpresets::loadOnTracks (engine, targets, f); });
        }
        menu.addSubMenu (tr ("File"), file);
    }
    if (t > 0)
    {
        const auto& mt = song.mixer[(std::size_t) t];
        const auto current = mt.inputChannel >= 0
                                 ? juce::String::fromUTF8 (inputSourceName (mt.inputChannel, mt.inputStereo, engine.getInputChannelNames()).c_str())
                                 : tr ("none");
        menu.addItem (tr ("Audio input: {0}...", { current }), [this, t] { mixerinput::showMenu (engine, t, this); });
    }
    // FL: the audio editor is loaded into the track's first empty slot (round 190cd); a full track gets the editor
    // window as before
    menu.addItem (tr ("Open audio editor") + "\tCtrl+E", [this, t] {
        if (const auto at = addEdisonSlot (engine, t); at >= 0)
            openEdisonSlot (engine, t, at);
        else
            openAudioEditor (engine);
    });
    menu.addItem (tr ("Open audio editor and record") + "\tShift+E", [this, t] {
        // FL arms an Edison set to On play, 5 min, Append off, Slave on (live check 190cq, shot edison190cd-05b)
        if (const auto at = addEdisonSlot (engine, t, true); at >= 0)
            openEdisonSlot (engine, t, at, true);
        else
            openAudioEditorRecording (engine);
    });
    menu.addSeparator(); // (FL: a line before Reset)
    menu.addItem (tr ("Reset selected track(s) to default"), [this, targets] { engine.editSong ([targets] (Song& s) { mixerops::resetToDefault (s, targets); }); });

    // --- FL's "Routing" section -----------------------------------------------------------------------
    menu.addSectionHeader (tr ("Routing"));
    {
        juce::PopupMenu channels;
        channels.addItem (tr ("Route selected channels to this track") + "\tCtrl+L", [this, t] {
            setSelection (selection, t);
            linkSelectedChannels (false);
        });
        channels.addItem (tr ("Route selected channels starting from this track") + "\tCtrl+Shift+L", [this, t] {
            setSelection (selection, t);
            linkSelectedChannels (true);
        });
        if (channel < (int) song.channels.size())
            channels.addItem (tr ("Route channel \"{0}\" here", { juce::String::fromUTF8 (song.channels[(std::size_t) channel].name.c_str()) }),
                              [this, channel, t] { engine.editSong ([channel, t] (Song& s) { s.channels[(std::size_t) channel].mixerTrack = t; }); });
        menu.addSubMenu (tr ("Channel routing"), channels);
    }
    {
        // FL's Routing submenu (REA round 165f-mixer: FUN_012ac520 / FUN_012ac950): selected tracks ⇄ this track
        std::vector<int> others;
        for (auto x : selection)
            if (x != t)
                others.push_back (x);
        const auto any = ! others.empty();
        juce::PopupMenu routing;
        routing.addSectionHeader (tr ("Selected tracks to this track"));
        using K = mixerops::RouteKind;
        auto selToThis = [this, others, t] (K kind) { engine.editSong ([others, t, kind] (Song& s) { mixerops::routeSelectedTo (s, others, t, kind); }); };
        auto thisToSel = [this, others, t] (K kind) { engine.editSong ([others, t, kind] (Song& s) { mixerops::routeToSelected (s, t, others, kind); }); };
        routing.addItem (tr ("Route selected to this track"), any, false, [selToThis] { selToThis (K::route); });
        routing.addItem (tr ("Route selected to this track only"), any, false, [selToThis] { selToThis (K::routeOnly); });
        routing.addItem (tr ("Sidechain selected to this track"), any && t > 0, false, [selToThis] { selToThis (K::sidechain); });
        routing.addItem (tr ("Sidechain selected to this track only"), any && t > 0, false, [selToThis] { selToThis (K::sidechainOnly); });
        routing.addSeparator();
        routing.addItem (tr ("Create submix..."), any && t > 0, false, [this, others, t] {
            const auto& track = engine.getSong().mixer[(std::size_t) t];
            const auto name = mixerops::hasDefaultName (engine.getSong(), t) ? juce::String() : juce::String::fromUTF8 (track.name.c_str());
            showNameAndColourDialog (tr ("Create submix"), name, track.colour, [this, others, t] (const juce::String& n, std::uint32_t colour) {
                if (t < (int) engine.getSong().mixer.size())
                    engine.editSong ([others, t, n = n.trim().toStdString(), colour] (Song& s) {
                        s.mixer[(std::size_t) t].colour = colour;
                        mixerops::createSubmix (s, others, t, n);
                    });
            });
        });
        const auto thisIsInsert = t > 0;
        routing.addSectionHeader (tr ("This track to selected tracks"));
        routing.addItem (tr ("Route this track to selected"), any && thisIsInsert, false, [thisToSel] { thisToSel (K::route); });
        routing.addItem (tr ("Route this track to selected only"), any && thisIsInsert, false, [thisToSel] { thisToSel (K::routeOnly); });
        routing.addItem (tr ("Sidechain this track to selected"), any && thisIsInsert, false, [thisToSel] { thisToSel (K::sidechain); });
        routing.addItem (tr ("Sidechain this track to selected only"), any && thisIsInsert, false, [thisToSel] { thisToSel (K::sidechainOnly); });
        menu.addSubMenu (tr ("Track routing"), routing, any);
        // round 186c's option: the sends this insert lost when their insert was deleted (greyed in the routing lane)
        if (t > 0 && ! song.mixer[(std::size_t) t].lostSends.empty())
        {
            juce::PopupMenu lost;
            const auto& lostSends = song.mixer[(std::size_t) t].lostSends;
            for (int i = 0; i < (int) lostSends.size(); ++i)
            {
                juce::PopupMenu one;
                juce::PopupMenu to;
                for (int x = 1; x < (int) song.mixer.size(); ++x)
                    if (x != t)
                        to.addItem (juce::String (x) + ". " + juce::String::fromUTF8 (song.mixer[(std::size_t) x].name.c_str()), song.routes (t, x) || song.canRoute (t, x),
                                    false, [this, t, i, x] { engine.editSong ([t, i, x] (Song& s) { mixerops::reconnectLostSend (s, t, i, x); }); });
                one.addSubMenu (tr ("Reconnect to"), to);
                one.addItem (tr ("Remove"), [this, t, i] { engine.editSong ([t, i] (Song& s) { mixerops::removeLostSends (s, t, i); }); });
                lost.addSubMenu (tr ("Send to {0} (deleted)", { juce::String::fromUTF8 (lostSends[(std::size_t) i].to.c_str()) }), one);
            }
            lost.addSeparator();
            lost.addItem (tr ("Remove all"), [this, t] { engine.editSong ([t] (Song& s) { mixerops::removeLostSends (s, t); }); });
            menu.addSubMenu (tr ("Disconnected links"), lost);
        }
    }
    {
        juce::PopupMenu select;
        select.addItem (tr ("Channels routed to this track") + "\tAlt+L", ! mixerops::channelsOn (song, t).empty(), false, [this, t] {
            setSelection (selection, t);
            selectLinkedChannels();
        });
        select.addSeparator();
        const auto destinations = mixerops::destinationsOf (song, t);
        const auto sources = mixerops::sourcesOf (song, t);
        select.addItem (tr ("Tracks this one is routed to"), ! destinations.empty(), false, [this, t, destinations] { setSelection (destinations, t); });
        select.addItem (tr ("Tracks routed to this one"), ! sources.empty(), false, [this, t, sources] { setSelection (sources, t); });
        select.addSeparator();
        select.addItem (tr ("Group"), t > 0, false, [this, t] {
            auto group = mixerops::groupOf (engine.getSong(), t);
            std::sort (group.begin(), group.end());
            setSelection (group, t);
        });
        select.addItem (tr ("All"), [this, t] {
            std::vector<int> all ((std::size_t) engine.getSong().mixer.size());
            std::iota (all.begin(), all.end(), 0);
            setSelection (all, t);
        });
        menu.addSubMenu (tr ("Select"), select);
    }
    menu.addItem (tr ("Reset selected track(s) routing"), [this, targets] { engine.editSong ([targets] (Song& s) { mixerops::resetRouting (s, targets); }); });
    menu.addItem (tr ("Unroute tracks routed to the selected track(s)"), [this, targets] { engine.editSong ([targets] (Song& s) { mixerops::unrouteInto (s, targets); }); });
    if (selected > 0 && selected != t && (t == 0 || song.canRoute (selected, t) || song.routes (selected, t)))
        menu.addItem (tr ("Send \"{0}\" only to this track", { juce::String::fromUTF8 (song.mixer[(std::size_t) selected].name.c_str()) }),
                      [this, from = selected, t] {
                          engine.editSong ([from, t] (Song& s) {
                              s.mixer[(std::size_t) from].sends.clear();
                              s.mixer[(std::size_t) from].toMaster = false;
                              s.setRoute (from, t, true);
                          });
                      });
    menu.addSeparator();
    {
        const auto inserts = [targets] {
            std::vector<int> v;
            for (auto x : targets)
                if (x > 0)
                    v.push_back (x);
            return v;
        }();
        // FL's two solo items (REA_rodada167l §4): the plain one mutes the tracks feeding the soloed ones, the alt one
        // keeps them; the same item again clears the solo
        menu.addItem (tr ("Solo selected track(s)") + "\tS", ! inserts.empty(), false, [this, inserts] {
            engine.editSong ([inserts] (Song& s) { mixerops::soloSelected (s, inserts, false); });
        });
        menu.addItem (tr ("Alt solo selected track(s)") + "\tAlt+S", ! inserts.empty(), false, [this, inserts] {
            engine.editSong ([inserts] (Song& s) { mixerops::soloSelected (s, inserts, true); });
        });
        menu.addItem (tr ("Arm selected track(s)"), ! inserts.empty(), false, [this, inserts] {
            for (auto x : inserts)
                if (! engine.isInsertArmed (x))
                    mixerinput::toggleArm (engine, x, this);
        });
        menu.addItem (tr ("Disarm selected track(s)"), ! inserts.empty(), false, [this, inserts] {
            for (auto x : inserts)
                if (engine.isInsertArmed (x))
                    engine.setInsertArmed (x, false);
        });
    }
    menu.addItem (tr ("Compensate for master latency"), song.mixer[(std::size_t) t].outputChannel >= 0, song.mixer[(std::size_t) t].compensateMasterLatency, [this, t] {
        engine.editSong ([t] (Song& s) { s.mixer[(std::size_t) t].compensateMasterLatency = ! s.mixer[(std::size_t) t].compensateMasterLatency; });
    });
    // FL's track latency (the "Track latency compensation" label's menu, REA round 165f-mixer)
    menu.addSubMenu (tr ("Track latency: {0}", { manualLatencyText (engine, song.mixer[(std::size_t) t]) }), trackLatencyMenu (engine, t));

    // --- LYNX's sound items (FL has them on the strip itself) -------------------------------------------
    menu.addSectionHeader (tr ("Sound"));
    {
        const auto& track = song.mixer[(std::size_t) t];
        juce::PopupMenu stereo;
        const std::pair<const char*, float> widths[] = { { TR_NOOP ("Mono"), 1.0f }, { TR_NOOP ("Narrower"), 0.5f }, { TR_NOOP ("Normal"), 0.0f },
                                                          { TR_NOOP ("Wider"), -0.5f }, { TR_NOOP ("Very wide"), -1.0f } };

        for (const auto& [label, value] : widths)
            stereo.addItem (tr (label), true, std::abs (track.stereo - value) < 0.01f,
                            [this, t, v = value] { engine.editSong ([t, v] (Song& s) { s.mixer[(std::size_t) t].stereo = v; }); });

        menu.addSubMenu (tr ("Stereo separation"), stereo);
        if (! song.cleanMixer)
            menu.addItem (tr ("Track EQ..."), [this, t] { showTrackEq (t); });
        menu.addItem (tr ("Invert phase"), true, track.invertPhase, [this, t] {
            engine.editSong ([t] (Song& s) { s.mixer[(std::size_t) t].invertPhase = ! s.mixer[(std::size_t) t].invertPhase; });
        });
        menu.addItem (tr ("Swap left/right"), true, track.swapChannels, [this, t] {
            engine.editSong ([t] (Song& s) { s.mixer[(std::size_t) t].swapChannels = ! s.mixer[(std::size_t) t].swapChannels; });
        });
        if (t > 0)
            menu.addItem (tr ("Render to audio (freeze)..."), [this, t] { bounceInsertToAudio (engine, t); });
        // round 190ch (LYNX's own, not an FL item): the one-click vocal clean-up chain on this insert. Round 190cm: here
        // with LYNX's other sound items, no longer between FL's own "File" and audio input rows
        if (t > 0)
            menu.addSubMenu (tr ("Clean voice"), lynx::ui::cleanVoiceMenu (engine, t));
    }

    // --- FL's "Group" section (REA round 146a) -----------------------------------------------------------
    if (t > 0)
    {
        menu.addSectionHeader (tr ("Group"));
        const auto on = song.mixer[(std::size_t) t].separator;
        menu.addItem (tr ("Separator"), true, on, [this, targets, on] {
            engine.editSong ([targets, on] (Song& s) { mixerops::setSeparator (s, targets, ! on); });
        });
        menu.addItem (tr ("Create group"), [this, targets, t] { engine.editSong ([targets, t] (Song& s) { mixerops::createGroup (s, targets, t); }); });
        menu.addItem (tr ("Auto color group"), song.mixer[(std::size_t) t].colour != 0, false,
                      [this, t] { engine.editSong ([t] (Song& s) { mixerops::autoColorGroup (s, t); }); });
        // LYNX's addition (round 177a): a group folds to its bus strip, or to a slim tab when it has no bus
        const auto canFold = mixerops::canFold (song, t);
        const auto folded = mixerops::isFolded (song, t);
        menu.addItem (folded ? tr ("Unfold group") : tr ("Fold group"), canFold, false, [this, t] {
            engine.editSong ([t] (Song& s) { mixerops::toggleFold (s, t); });
            updateSize();
        });
        menu.addItem (tr ("Fold all groups"), [this] {
            engine.editSong ([] (Song& s) { mixerops::foldAll (s, true); });
            updateSize();
        });
        menu.addItem (tr ("Unfold all groups"), [this] {
            engine.editSong ([] (Song& s) { mixerops::foldAll (s, false); });
            updateSize();
        });

        // LYNX's addition (round 177a): VCA faders, their strips left of the Master
        std::vector<int> members;
        for (const auto m : targets)
            if (m > 0)
                members.push_back (m);
        juce::PopupMenu vcaMenu;
        vcaMenu.addItem (tr ("New VCA from selected"), [this, members] {
            engine.editSong ([members] (Song& s) { mixerops::createVca (s, members, {}); });
            updateSize();
        });
        juce::PopupMenu assign, remove;
        for (int i = 0; i < (int) song.vcas.size(); ++i)
        {
            const auto& vca = song.vcas[(std::size_t) i];
            const auto in = [&vca] (int m) { return std::binary_search (vca.members.begin(), vca.members.end(), m); };
            const auto all = std::all_of (members.begin(), members.end(), in);
            const auto any = std::any_of (members.begin(), members.end(), in);
            const auto name = juce::String::fromUTF8 (vca.name.c_str());
            assign.addItem (name, ! all, all, [this, members, i] {
                engine.editSong ([members, i] (Song& s) { mixerops::assignToVca (s, members, i); });
            });
            if (any)
                remove.addItem (name, [this, members, i] {
                    engine.editSong ([members, i] (Song& s) { mixerops::removeFromVca (s, members, i); });
                });
        }
        vcaMenu.addSubMenu (tr ("Assign to VCA"), assign, assign.getNumItems() > 0);
        vcaMenu.addSubMenu (tr ("Remove from VCA"), remove, remove.getNumItems() > 0);
        menu.addSubMenu (tr ("VCA"), vcaMenu);
    }

    // --- FL's "Layout" section --------------------------------------------------------------------------
    {
        menu.addSectionHeader (tr ("Layout"));
        juce::PopupMenu dockMenu;
        const auto dock = song.mixer[(std::size_t) t].dock;
        const std::pair<const char*, MixerDock> docks[] = { { TR_NOOP ("Left"), MixerDock::left }, { TR_NOOP ("Middle"), MixerDock::middle },
                                                            { TR_NOOP ("Right"), MixerDock::right } };
        for (const auto& [label, d] : docks)
            dockMenu.addItem (tr (label), true, dock == d, [this, targets, d = d] {
                engine.editSong ([targets, d] (Song& s) { mixerops::dockTracks (s, targets, d); });
            });
        menu.addSubMenu (tr ("Dock to"), dockMenu);
    }
    if (t > 0)
    {
        menu.addItem (tr ("Move left") + "\tAlt+Left", t > 1, false, [this, t] {
            setSelection ({ t }, t);
            moveSelected (-1);
        });
        menu.addItem (tr ("Move right") + "\tAlt+Right", t + 1 < (int) song.mixer.size(), false, [this, t] {
            setSelection ({ t }, t);
            moveSelected (1);
        });
        menu.addSeparator();
        menu.addItem (tr ("Delete") + "\tDel", [this, targets] { deleteTracks (targets); });
        const auto room = (int) song.mixer.size() < maxMixerTracks;
        menu.addItem (tr ("Insert one"), room, false, [this, t] {
            engine.editSong ([t] (Song& s) { mixerops::insertTrack (s, t); });
            setSelection ({ t }, t);
        });
        menu.addItem (tr ("Insert multiple..."), room, false, [this, t] { askInsertMultiple (t); });
        juce::PopupMenu fromState;
        for (const auto& f : mixerpresets::all())
            fromState.addItem (f.getFileNameWithoutExtension(), room, false, [this, t, f] {
                if (const auto added = mixerpresets::insertTrack (engine, t, f); added > 0)
                    setSelection ({ added }, added);
            });
        menu.addSubMenu (tr ("Insert from state"), fromState, room && fromState.getNumItems() > 0);

        // FL's last section, "CPU" (REA round 134)
        menu.addSectionHeader (tr ("CPU"));
        menu.addItem (tr ("Allow threaded processing"), true, song.mixer[(std::size_t) t].threaded, [this, t] {
            engine.editSong ([t] (Song& s) { s.mixer[(std::size_t) t].threaded = ! s.mixer[(std::size_t) t].threaded; });
        });
    }

    menu.addSectionHeader (tr ("Mixer"));
    {
        // FL's Mixer › Disk recording (REA round 165f-mixer: FUN_012a7a80 / FUN_012a7d80)
        std::vector<int> armed;
        for (int x = 1; x < (int) song.mixer.size(); ++x)
            if (engine.isInsertArmed (x))
                armed.push_back (x);
        juce::PopupMenu disk;
        auto& eng = engine;
        disk.addItem (tr ("Render armed tracks to wave file(s)...") + "\tAlt+R", ! armed.empty(), false, [armed, &eng] {
            if (mixeroptions::renderTracks)
                mixeroptions::renderTracks (armed, [armed, &eng] (const std::vector<juce::File>& files) {
                    if (eng.getRecordOptions().unarmAfterRender) // FL's "Auto-unarm" (round 166e)
                        for (auto t : armed)
                            eng.setInsertArmed (t, false);
                    if (eng.getRecordOptions().autoCreateClip) // FL's "Auto-create audio clip" (round 167a)
                        placeRenderedFiles (eng, files);
                });
        });
        disk.addItem (tr ("Render selected tracks to wave file(s)...") + "\tShift+Alt+R", ! targets.empty(), false, [targets] {
            if (mixeroptions::renderTracks)
                mixeroptions::renderTracks (targets, {});
        });
        // FL's Disk recording "Options" (round 166e) and LYNX's own disarm after a take (issue #16)
        disk.addSectionHeader (tr ("Options"));
        const auto& ro = engine.getRecordOptions();
        auto setRecord = [&eng] (std::function<void (AudioEngine::RecordOptions&)> change) {
            auto o = eng.getRecordOptions();
            change (o);
            eng.setRecordOptions (o);
            if (mixeroptions::changed)
                mixeroptions::changed();
        };
        disk.addItem (tr ("Auto-unarm (rendering the armed tracks disarms them)"), true, ro.unarmAfterRender,
                      [setRecord] { setRecord ([] (AudioEngine::RecordOptions& r) { r.unarmAfterRender = ! r.unarmAfterRender; }); });
        disk.addItem (tr ("Auto-create audio clip (the rendered files go on the Playlist)"), true, ro.autoCreateClip,
                      [setRecord] { setRecord ([] (AudioEngine::RecordOptions& r) { r.autoCreateClip = ! r.autoCreateClip; }); });
        // FL's "Latency compensation" (RecLatCompMenu, checked by default; round 190dx): off, recorded takes are not moved
        // back by the round trip, the fine adjust and the PDC (Options › Audio › Recording)
        disk.addItem (tr ("Latency compensation"), true, ro.latencyCompensation,
                      [setRecord] { setRecord ([] (AudioEngine::RecordOptions& r) { r.latencyCompensation = ! r.latencyCompensation; }); });
        disk.addItem (tr ("Disarm the inserts when recording stops"), true, ro.disarmOnStop,
                      [setRecord] { setRecord ([] (AudioEngine::RecordOptions& r) { r.disarmOnStop = ! r.disarmOnStop; }); });
        // LYNX's own (round 190aq): also in the record button's menu
        disk.addItem (tr ("Low latency while recording (bypasses effects that add latency on armed or monitored inserts)"), true, ro.lowLatencyRecording,
                      [setRecord] { setRecord ([] (AudioEngine::RecordOptions& r) { r.lowLatencyRecording = ! r.lowLatencyRecording; }); });
        // FL's "Format" group (round 190bf, fl190be bonus shot): the recorded takes' sample format, radio items (the
        // same setting as Options › Audio › Recording › File format)
        disk.addSectionHeader (tr ("Format"));
        {
            const auto now = eng.getRecordingSettings().format;
            const std::pair<takes::SampleFormat, const char*> formats[] { { takes::SampleFormat::pcm16, TR_NOOP ("16Bit int") },
                                                                          { takes::SampleFormat::pcm24, TR_NOOP ("24Bit int") },
                                                                          { takes::SampleFormat::float32, TR_NOOP ("32Bit float") } };
            for (const auto& [format, name] : formats)
                quickpick::addRadioItem (disk, tr (name), now == format, [&eng, format = format] {
                    auto r = eng.getRecordingSettings();
                    r.format = format;
                    eng.setRecordingSettings (r);
                });
        }
        menu.addSubMenu (tr ("Disk recording"), disk);
        juce::PopupMenu pdc;
        const auto manual = std::count_if (song.mixer.begin(), song.mixer.end(), [] (const MixerTrack& m) { return m.latencyMs != 0.0f; });
        pdc.addItem (tr ("Reset manual latency on all tracks"), manual > 0, false, [this] {
            juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::WarningIcon, tr ("Warning"),
                                                tr ("This will reset manual latency values on all mixer tracks."), tr ("OK"), tr ("Cancel"), nullptr,
                                                juce::ModalCallbackFunction::create ([this] (int r) {
                                                    if (r == 1)
                                                        engine.editSong ([] (Song& s) { mixerops::resetManualLatency (s); });
                                                }));
        });
        // FL's Automatic / Compensate automations (REA round 168j, .flp 0x1D / 0x27; FL's AutoPDCMenuClick toggles it)
        pdc.addItem (tr ("Automatic (compensate the plugins' latencies)"), true, song.autoPdc,
                     [this] { engine.editSong ([] (Song& s) { s.autoPdc = ! s.autoPdc; }); });
        pdc.addItem (tr ("Compensate automations (delay a plugin's automation with its audio)"), true, song.compensateAutomations,
                     [this] { engine.editSong ([] (Song& s) { s.compensateAutomations = ! s.compensateAutomations; }); });
        menu.addSubMenu (tr ("Plugin delay compensation"), pdc);
    }
    menu.addSubMenu (tr ("Mixer layout"), MixerLayoutBox::menu (*this)); // as the title bar's drop-down (issue #52)
    mixeroptions::addItems (menu, engine, false);

    return menu;
}

void MixerStrips::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // only levels moved since the last call (a fader / knob drag, AudioEngine::editLevels; round 171c): repaint just
    // the strips whose fader, pan or separation changed instead of the whole mixer at every mouse move
    const auto& mixer = engine.getSong().mixer;
    if (engine.getPublishCount() == seenPublish && shownLevels.size() == mixer.size())
    {
        const auto& vcas = engine.getSong().vcas;
        if (vcaLevelsShown.size() == vcas.size())
            for (std::size_t i = 0; i < vcas.size(); ++i)
                if (vcas[i].level != vcaLevelsShown[i])
                {
                    vcaLevelsShown[i] = vcas[i].level;
                    repaintVca ((int) i);
                }
        for (std::size_t t = 0; t < mixer.size(); ++t)
        {
            const std::array<float, 3> now { mixer[t].volume, mixer[t].pan, mixer[t].stereo };
            if (now != shownLevels[t])
            {
                // round 173f: just the control that moved (the fader with its cap and dB read-out, or a knob)
                const auto track = (int) t;
                if (now[0] != shownLevels[t][0])
                {
                    const auto fader = faderArea (track);
                    repaint (stripX (track), fader.getY() - 9, stripWidthOf (track), fader.getHeight() + 9 + 20);
                }
                if (now[1] != shownLevels[t][1])
                    repaint (panArea (track).expanded (2));
                if (now[2] != shownLevels[t][2])
                    repaint (stereoArea (track).expanded (2));
                shownLevels[t] = now;
            }
        }
        return;
    }
    seenPublish = engine.getPublishCount();
    // "Mixer groups follow the Playlist" (round 186c): a Playlist group edit regroups the mixer, outside the undo history
    // (an undo of the Playlist edit brings the old groups back through this same path)
    if (followPlaylist && ! followPending)
    {
        if (! mixerops::followsPlaylistGroups (engine.getSong()))
        {
            followPending = true;
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MixerStrips> (this)] {
                if (safe == nullptr)
                    return;
                safe->engine.editSong ([] (Song& s) { mixerops::followPlaylistGroups (s); }, false);
                safe->followPending = false;
            });
        }
    }
    vcaLevelsShown.clear();
    for (const auto& vca : engine.getSong().vcas)
        vcaLevelsShown.push_back (vca.level);
    shownLevels.resize (mixer.size());
    for (std::size_t t = 0; t < mixer.size(); ++t)
        shownLevels[t] = { mixer[t].volume, mixer[t].pan, mixer[t].stereo };

    selected = juce::jlimit (0, juce::jmax (0, (int) engine.getSong().mixer.size() - 1), selected);
    const auto count = (int) engine.getSong().mixer.size();
    selection.erase (std::remove_if (selection.begin(), selection.end(), [count] (int t) { return t >= count; }), selection.end());
    if (! std::binary_search (selection.begin(), selection.end(), selected))
        selection = { selected };
    updateSize();
    repaint();
}

float MixerStrips::shownVolume (int t) const
{
    const auto& mixer = engine.getSong().mixer;
    const auto base = t >= 0 && t < (int) mixer.size() ? mixer[(std::size_t) t].volume : 0.0f;
    return engine.getAutomationDisplay().shown (AutomationTarget { AutomationTargetKind::mixerVolume, t }, base);
}

void MixerStrips::repaintAutomatedStrips()
{
    // faders, pan and send knobs follow their automation while the song plays (issue #12): only the automated controls
    // repaint, and only when the value they show changed (round 174e: just the fader column or the knob, not the strip)
    const auto& shown = engine.getAutomationDisplay();
    if (shown.getGeneration() == shownAutomation)
        return;
    shownAutomation = shown.getGeneration();

    std::vector<int> strips;
    sendAutomatedStrips.clear();
    for (const auto& t : shown.automatedTargets())
    {
        if (t.kind == AutomationTargetKind::mixerVolume || t.kind == AutomationTargetKind::mixerPan)
            strips.push_back (t.index);
        else if (t.kind == AutomationTargetKind::mixerSend && t.index == selected)
        {
            strips.push_back (t.slot); // (the route knob sits under the destination)
            sendAutomatedStrips.push_back (t.slot);
        }
        else if (t.kind == AutomationTargetKind::vcaLevel && isShowing())
            repaintVca (t.index);
    }
    std::vector<int> all = strips;
    all.insert (all.end(), automatedStrips.begin(), automatedStrips.end());
    automatedStrips = std::move (strips);
    if (! isShowing())
        return;
    std::sort (all.begin(), all.end());
    all.erase (std::unique (all.begin(), all.end()), all.end());
    const auto& mixer = engine.getSong().mixer;
    const auto count = (int) mixer.size();
    automatedShown.resize (mixer.size(), { -1.0f, -1.0f });
    for (const auto t : all)
    {
        if (t < 0 || t >= count || isHidden (t) || isFoldTab (t)) // (a folded member has no control on screen to move)
            continue;
        const std::array<float, 2> now { shownVolume (t),
                                         shown.shown (AutomationTarget { AutomationTargetKind::mixerPan, t }, (mixer[(std::size_t) t].pan + 1.0f) * 0.5f) };
        auto& was = automatedShown[(std::size_t) t];
        if (now[0] != was[0])
        {
            // round 174e2: the cap's rail when the cap moved by an eighth of a pixel, the dB readout when its text changed
            // (not the strip's whole column: the meters beside it stay in the cached picture)
            const auto rail = faderRail (t);
            const auto pixels = [&rail] (float v) { return juce::roundToInt (v * (float) rail.getHeight() * 8.0f); };
            if (was[0] < 0.0f || pixels (now[0]) != pixels (was[0]))
                repaint (rail.getX() - 3, rail.getY() - 9, rail.getWidth() + 6, rail.getHeight() + 18);
            if (spec().dbReadout && (was[0] < 0.0f || faderToDb (now[0]) != faderToDb (was[0])))
                repaint (stripX (t), faderArea (t).getBottom() + 3, stripWidthOf (t) - spec().gap, 14);
        }
        if (now[1] != was[1])
            repaint (panArea (t).expanded (2));
        if (std::find (sendAutomatedStrips.begin(), sendAutomatedStrips.end(), t) != sendAutomatedStrips.end())
            repaint (routeArea (t).expanded (2)); // (a send knob: small; round 174e2: only when a send is automated)
        was = now;
    }
}

namespace
{
// issue #144: a silenced track's meter colours (FL draws them darker and greyer)
juce::Colour silencedMeterColour (juce::Colour c) { return c.withMultipliedSaturation (0.55f).withMultipliedBrightness (0.62f); }
} // namespace

// The two thin level bars beside a vertical name (Wide layouts); empty when the layout has none
juce::Rectangle<int> MixerStrips::nameBars (int t) const
{
    const auto& sp = spec();
    if (! sp.verticalName || sp.nameLow || mixerlayout::isCompact (layout) || t <= 0)
        return {};
    const auto box = nameArea (t);
    const auto plateWidth = stripWidthOf (t) - sp.gap;
    return { box.getRight() + 2, box.getY() + 6, plateWidth - box.getWidth() - 6, box.getHeight() - 12 };
}

void MixerStrips::paintNameBars (juce::Graphics& g, int t) const
{
    using namespace theme;
    const auto bars = nameBars (t);
    for (int ch = 0; ch < 2; ++ch)
    {
        const auto index = (std::size_t) t * 2 + (std::size_t) ch;
        const auto level = index < levels.size() ? levels[index].level : 0.0f;
        const auto bw = (bars.getWidth() - 3) / 2;
        const auto bar = juce::Rectangle<int> (bars.getX() + 1 + ch * (bw + 1), bars.getY() + 1, bw, bars.getHeight() - 2);
        const auto h = (int) (meterPosition (level) * (float) bar.getHeight());
        if (h <= 0)
            continue;
        const auto c = level >= 1.0f ? colour::meterClip : level >= 0.5f ? colour::meterMid : colour::meterLow;
        g.setColour (isSilenced (t) ? silencedMeterColour (c) : c); // (issue #144: dimmed while silenced)
        g.fillRect (bar.withTop (bar.getBottom() - h));
    }
}

// Issue #144: a silenced track (its mute, a solo elsewhere, a muted VCA) keeps its meter, dimmed, as FL draws it
bool MixerStrips::isSilenced (int t) const
{
    const auto& song = engine.getSong();
    if (t < 0 || t >= (int) song.mixer.size())
        return false;
    if (song.mixer[(std::size_t) t].muted)
        return true;
    if (t == 0)
        return false;
    if (mixerops::vcaMuted (song, t))
        return true;
    if (std::none_of (song.mixer.begin(), song.mixer.end(), [] (const MixerTrack& m) { return m.solo; }))
        return false;
    const auto audible = song.soloAudible();
    return (std::size_t) t < audible.size() && ! audible[(std::size_t) t];
}

// --- the loudness panel beside the Master (round 190am, LYNX's own; Mixer menu, off by default) -----------------------
namespace
{
constexpr float loudnessBarTop = 0.0f, loudnessBarBottom = -42.0f; // LUFS at the bar's ends
float loudnessBarPosition (float lufs) noexcept
{
    return juce::jlimit (0.0f, 1.0f, (lufs - loudnessBarBottom) / (loudnessBarTop - loudnessBarBottom));
}
constexpr int loudnessRowsTop = 24, loudnessRowHeight = 16;
} // namespace

void MixerStrips::setShowLoudness (bool on)
{
    showLoudness = on;
    engine.getMeters().loudness.setEnabled (on); // (off: the audio thread measures nothing)
    loudnessShown = {};
    updateSize();
    repaint();
}

void MixerStrips::resetLoudness()
{
    engine.getMeters().loudness.reset();
    loudnessShown = {};
    repaint (loudnessArea());
}

juce::Rectangle<int> MixerStrips::loudnessArea() const noexcept
{
    if (! showLoudness || loudnessX == hiddenX)
        return {};
    return { loudnessX + pinOffset (dockOf (0)), 2, loudnessWidth - spec().gap, laneTop() - 2 };
}

juce::Rectangle<int> MixerStrips::loudnessResetArea() const noexcept
{
    const auto area = loudnessArea();
    if (area.isEmpty())
        return {};
    return { area.getX() + 4, rows().faderBottom + 3, area.getWidth() - 8, 15 };
}

juce::Rectangle<int> MixerStrips::loudnessBarArea() const noexcept
{
    const auto area = loudnessArea();
    const auto r = rows();
    return { area.getX() + 6, r.faderTop, 8, r.faderBottom - r.faderTop };
}

void MixerStrips::paintLoudnessPanel (juce::Graphics& g) const
{
    using namespace theme;
    const auto area = loudnessArea();
    theme::fillRoundRect (g, area.toFloat(), radius::md, colour::bg2);
    g.setColour (colour::border);
    g.drawRoundedRectangle (area.toFloat().reduced (0.5f), radius::md, 1.0f);

    g.setColour (colour::textSecondary);
    g.setFont (font (type::caption, true));
    textcache::drawFittedText (g, "LUFS", juce::Rectangle<int> (area.getX(), 4, area.getWidth(), 13), juce::Justification::centred, 1, 0.7f);

    // the captions of the three values (EBU R 128's letters; the hint bar spells them out)
    g.setFont (font (9.0f, true));
    const char* captions[] = { "S", "I", "TP" };
    for (int i = 0; i < 3; ++i)
        textcache::drawText (g, captions[i], juce::Rectangle<int> (area.getX() + 5, loudnessRowsTop + i * loudnessRowHeight, 14, loudnessRowHeight),
                             juce::Justification::centredLeft, false);

    // the short-term bar's well and scale (LUFS, 6 LU apart, as many as fit)
    const auto bar = loudnessBarArea();
    theme::fillRoundRect (g, bar.toFloat(), 1.5f, colour::bg0);
    g.setFont (font (9.0f));
    const auto textHeight = 10.0f;
    auto lastY = -100.0f;
    for (int lufs = 0; lufs >= (int) loudnessBarBottom; lufs -= 6)
    {
        const auto y = (float) bar.getBottom() - loudnessBarPosition ((float) lufs) * (float) bar.getHeight();
        if (y - lastY < textHeight + 1.0f)
            continue;
        lastY = y;
        g.setColour (colour::textSecondary);
        g.fillRect (juce::Rectangle<float> ((float) bar.getRight() + 1.0f, y - 0.5f, 3.0f, 1.0f));
        const auto box = juce::Rectangle<float> ((float) bar.getRight() + 5.0f, y - textHeight * 0.5f, (float) (area.getRight() - bar.getRight()) - 9.0f, textHeight)
                             .withY (juce::jlimit ((float) bar.getY(), (float) bar.getBottom() - textHeight, y - textHeight * 0.5f));
        textcache::drawText (g, juce::String (lufs), box, juce::Justification::centredRight, false);
    }

    // Reset
    const auto reset = loudnessResetArea().toFloat();
    theme::fillRoundRect (g, reset, radius::sm, loudnessResetHovered ? colour::bg5 : colour::bg0);
    g.setColour (loudnessResetHovered ? colour::textPrimary : colour::textSecondary);
    g.setFont (font (type::caption));
    textcache::drawFittedText (g, tr ("Reset"), reset.toNearestInt().reduced (2, 0), juce::Justification::centred, 1, 0.6f);
}

void MixerStrips::paintLoudnessValues (juce::Graphics& g) const
{
    using namespace theme;
    const auto area = loudnessArea();
    const auto& v = loudnessShown;
    const float values[] = { v.shortTerm, v.integrated, v.truePeak };
    g.setFont (font (type::small, true));
    for (int i = 0; i < 3; ++i)
    {
        const auto known = MasterLoudness::measured (values[i]);
        // the true peak warns like the peak meters: amber from -1 dBTP, red above 0 dBTP
        g.setColour (! known ? colour::textDisabled
                             : i == 2 && values[i] > 0.0f ? colour::meterClip : i == 2 && values[i] > -1.0f ? colour::meterMid : colour::textPrimary);
        textcache::drawFittedText (g, juce::String (formatLoudness (values[i])),
                                   juce::Rectangle<int> (area.getX() + 18, loudnessRowsTop + i * loudnessRowHeight, area.getWidth() - 23, loudnessRowHeight),
                                   juce::Justification::centredRight, 1, 0.7f);
    }

    const auto bar = loudnessBarArea();
    if (MasterLoudness::measured (v.shortTerm))
    {
        const auto h = juce::roundToInt (loudnessBarPosition (v.shortTerm) * (float) bar.getHeight());
        g.setColour (colour::meterLow);
        g.fillRect (bar.withTop (bar.getBottom() - h).reduced (1, 0));
    }
    if (MasterLoudness::measured (v.integrated)) // the integrated loudness: a line across the bar
    {
        const auto y = (float) bar.getBottom() - loudnessBarPosition (v.integrated) * (float) bar.getHeight();
        g.setColour (colour::textPrimary);
        g.fillRect (juce::Rectangle<float> ((float) bar.getX() - 2.0f, y - 1.0f, (float) bar.getWidth() + 4.0f, 2.0f));
    }
}

void MixerStrips::showLoudnessMenu()
{
    juce::PopupMenu menu;
    menu.addItem (tr ("Reset"), [this] { resetLoudness(); });
    menu.addSeparator();
    menu.addItem (tr ("Loudness meter beside the Master (LYNX)"), true, true, [] {
        if (mixeroptions::setShowLoudness)
            mixeroptions::setShowLoudness (false);
        if (mixeroptions::changed)
            mixeroptions::changed();
    });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

// One meter's level, peak hold line and clip latch, inside its well (painted with the strip, or over the cached picture)
void MixerStrips::paintMeterLevel (juce::Graphics& g, int t, int ch, juce::Rectangle<int> bar) const
{
    using namespace theme;
    const auto index = (std::size_t) t * 2 + (std::size_t) ch;
    const auto meter = index < levels.size() ? levels[index] : meters::PeakHold {};
    const auto h = (int) (meterPosition (meter.level) * (float) bar.getHeight());
    const auto silenced = (h > 0 || meter.peak > 0.001f) && isSilenced (t);
    const auto tone = [silenced] (juce::Colour c) { return silenced ? silencedMeterColour (c) : c; };
    if (h > 0)
    {
        const auto lit = bar.withTop (bar.getBottom() - h);
        // (vertical: every meter of the same height shares one gradient, so JUCE's Direct2D brush cache hits)
        juce::ColourGradient gradient (tone (colour::meterClip), 0.0f, (float) bar.getY(), tone (colour::meterLow), 0.0f, (float) bar.getBottom(), false);
        gradient.addColour (1.0 - meterPosition (juce::Decibels::decibelsToGain (0.0f)), tone (colour::meterClip)); // above 0 dB: clip zone
        gradient.addColour (1.0 - meterPosition (juce::Decibels::decibelsToGain (-6.0f)), tone (colour::meterMid));
        gradient.addColour (1.0 - meterPosition (juce::Decibels::decibelsToGain (-18.0f)), tone (colour::meterLow));
        g.setGradientFill (gradient);
        g.fillRect (lit);
    }

    if (meter.peak > 0.001f) // peak hold line
    {
        const auto peakY = bar.getBottom() - (int) (meterPosition (meter.peak) * (float) bar.getHeight());
        g.setColour (tone (meter.peak >= 1.0f ? colour::meterClip : meter.peak >= 0.5f ? colour::meterMid : colour::textPrimary.withAlpha (0.85f)));
        g.fillRect (bar.getX(), juce::jmax (bar.getY(), peakY - 1), bar.getWidth(), 2);
    }

    if (meter.clipped) // clip latch: stays until the meter is clicked
    {
        g.setColour (colour::meterClip);
        g.fillRect (bar.withHeight (4));
    }
}

// The levels over the cached picture: every meter the clip reaches, except where a pinned dock block covers a scrolled
// strip (paint() draws the pinned blocks over the middle one)
void MixerStrips::paintMeterLevels (juce::Graphics& g) const
{
    const auto clip = g.getClipBounds();
    const auto& sp = spec();
    std::vector<juce::Rectangle<int>> pinned;
    for (auto dock : { MixerDock::left, MixerDock::right })
        if (const auto shift = pinOffset (dock); shift != 0)
        {
            const auto d = (int) dock;
            pinned.emplace_back (blockStart[d] + shift, 0, blockEnd[d] - blockStart[d], getHeight());
        }
    const auto tracks = (int) engine.getSong().mixer.size();
    for (int t = 0; t < tracks; ++t)
    {
        if (isHidden (t))
            continue;
        if (isFoldTab (t)) // (a folded group's tab: one bar, and no meter of its own)
        {
            const auto rail = faderRail (t);
            if (const juce::Rectangle<int> bar (stripX (t), rail.getY(), foldTabWidth, rail.getHeight()); bar.intersects (clip))
            {
                const juce::Graphics::ScopedSaveState state (g);
                if (dockOf (t) == MixerDock::middle)
                    for (const auto& r : pinned)
                        g.excludeClipRegion (r);
                paintFoldTabLevel (g, t);
            }
            continue;
        }
        const auto meters = meterArea (t);
        const auto bars = nameBars (t);
        const auto meterShown = meters.intersects (clip) && g.clipRegionIntersects (meters);
        const auto barsShown = ! bars.isEmpty() && bars.intersects (clip) && g.clipRegionIntersects (bars);
        if (! meterShown && ! barsShown)
            continue;
        const juce::Graphics::ScopedSaveState state (g);
        if (dockOf (t) == MixerDock::middle)
            for (const auto& r : pinned)
                g.excludeClipRegion (r);
        if (meterShown)
            for (int ch = 0; ch < 2; ++ch)
                paintMeterLevel (g, t, ch, juce::Rectangle<int> (meters.getX() + ch * (sp.meterWidth + 2), meters.getY(), sp.meterWidth, meters.getHeight()));
        if (barsShown)
            paintNameBars (g, t);
    }
    for (int i = 0; i < (int) vcaXs.size(); ++i)
        if (meterArea (vcaStrip (i)).intersects (clip))
        {
            const juce::Graphics::ScopedSaveState state (g);
            if (dockOf (0) == MixerDock::middle) // (the VCA block sits in the Master's dock)
                for (const auto& r : pinned)
                    g.excludeClipRegion (r);
            paintVcaMeter (g, i);
        }
    if (showLoudness && loudnessArea().intersects (clip))
    {
        const juce::Graphics::ScopedSaveState state (g);
        if (dockOf (0) == MixerDock::middle)
            for (const auto& r : pinned)
                g.excludeClipRegion (r);
        paintLoudnessValues (g);
    }
}

bool MixerStrips::liveFrame (double nowMs)
{
    auto& meters = engine.getMeters();
    const auto showing = isShowing();
    repaintAutomatedStrips();
    const auto elapsed = lastFrameMs > 0.0 ? juce::jlimit (0.0, 250.0, nowMs - lastFrameMs) : 1000.0 / 30.0;
    lastFrameMs = nowMs;
    const auto& mixer = engine.getSong().mixer;
    inputLevels.resize (mixer.size(), 0.0f);
    auto moving = engine.getTransport().playing.load (std::memory_order_relaxed);

    const auto inputFall = ui::fallFor (0.8f, elapsed);
    for (std::size_t t = 1; t < mixer.size(); ++t)
    {
        const auto peak = mixer[t].inputChannel >= 0 ? juce::jmax (engine.takeInputPeak ((int) t, 0), engine.takeInputPeak ((int) t, 1)) : 0.0f;
        auto level = juce::jmax (peak, inputLevels[t] * inputFall);
        level = level < 1e-4f ? 0.0f : level;
        if (std::abs (level - inputLevels[t]) > 1e-4f)
        {
            moving = true;
            inputLevels[t] = level;
            if (showing)
                repaint (inputArea ((int) t).expanded (1));
        }
    }

    // round 177d: the gain-reduction meters (strips with one of LYNX's own dynamics only; nothing while hidden)
    if (showing)
    {
        grShown.resize (mixer.size(), -1);
        for (int t = 1; t < (int) mixer.size(); ++t)
        {
            const auto gr = gainReduction (t);
            const auto h = gr < 0.0f ? -1 : juce::roundToInt (gr * (float) grArea (t).getHeight());
            if (h != grShown[(std::size_t) t])
            {
                grShown[(std::size_t) t] = h;
                repaint (grArea (t).expanded (1));
            }
            moving = moving || h > 0;
        }
    }

    // every meter row that changed by a pixel goes into one rectangle, repainted once per frame (round 174e2: the A/B
    // against one repaint per meter favoured the single rectangle, within the noise of the machine)
    juce::Rectangle<int> dirty;
    int woken = -1;
    for (std::size_t i = 0; i < levels.size(); ++i)
    {
        auto& m = levels[i];
        const auto before = m;
        const auto peak = meters.takePeak ((int) (i / 2), (int) (i % 2));
        m.advance (peak, elapsed); // ~20 dB/s falloff, 1.5 s peak hold
        // "Show only used inserts" (round 190am): an insert that receives audio always shows, whatever feeds it
        if (showUsedOnly && peak > 1.0e-4f && i >= 2 && revealed.count ((int) (i / 2)) == 0 && isHidden ((int) (i / 2)))
            woken = (int) (i / 2);
        if (m.level != before.level || m.peak != before.peak)
            moving = true;
        if (! showing)
            continue;

        // only the rows of the meter that changed by a pixel are repainted (round 174e: at the display's rate), and in
        // the Wide layouts the two thin bars beside the name, which follow the same level (round 173f)
        const auto t = (int) (i / 2);
        const auto ch = (int) (i % 2);
        if (t < (int) tabOf.size() && tabOf[(std::size_t) t] > 0 && m.level != before.level) // a folded group's tab
        {
            const auto tab = tabOf[(std::size_t) t];
            const auto rail = faderRail (tab);
            const juce::Rectangle<int> bar (stripX (tab) + foldTabWidth - 10, rail.getY() - 1, 8, rail.getHeight() + 2);
            dirty = dirty.isEmpty() ? bar : dirty.getUnion (bar);
        }
        if (isHidden (t) || isFoldTab (t))
            continue;
        const auto area = meterArea (t);
        const auto& sp = spec();
        const auto rows = meters::dirtyRows (before, m, area.getHeight(), [] (float v) { return meterPosition (v); });
        if (! rows.empty())
        {
            const juce::Rectangle<int> bar (area.getX() + ch * (sp.meterWidth + 2) - 1, area.getY() + rows.top - 1, sp.meterWidth + 2,
                                            rows.bottom - rows.top + 2);
            dirty = dirty.isEmpty() ? bar : dirty.getUnion (bar);
            // the thin bars beside the name: only the bar that moved, and (cached) over the picture as well (round 174e2)
            if (const auto bars = nameBars (t); ! bars.isEmpty()
                && (int) (meterPosition (m.level) * 64.0f) != (int) (meterPosition (before.level) * 64.0f))
            {
                const auto bw = (bars.getWidth() - 3) / 2;
                dirty = dirty.getUnion ({ bars.getX() + 1 + ch * (bw + 1) - 1, bars.getY(), bw + 2, bars.getHeight() });
            }
        }
    }

    // the VCA strips' meters show their loudest member (round 177a; nothing to do without a VCA)
    if (const auto count = engine.getSong().vcas.size(); count > 0 && showing)
    {
        vcaMetersShown.resize (count * 2, 0.0f);
        for (int i = 0; i < (int) count; ++i)
            for (int ch = 0; ch < 2; ++ch)
            {
                const auto level = vcaMeter (i, ch);
                auto& was = vcaMetersShown[(std::size_t) i * 2 + (std::size_t) ch];
                if ((int) (meterPosition (level) * 256.0f) != (int) (meterPosition (was) * 256.0f))
                {
                    const auto area = meterArea (vcaStrip (i)).expanded (1);
                    dirty = dirty.isEmpty() ? area : dirty.getUnion (area);
                }
                was = level;
            }
    }
    // the loudness panel (round 190am; nothing while it is off): repainted when a shown number or the bar changes
    if (showLoudness && showing)
        if (const auto now = meters.loudness.read(); ! (now == loudnessShown))
        {
            const auto same = [] (float a, float b) { return formatLoudness (a) == formatLoudness (b); };
            const auto changed = ! same (now.shortTerm, loudnessShown.shortTerm) || ! same (now.integrated, loudnessShown.integrated)
                                 || ! same (now.truePeak, loudnessShown.truePeak);
            loudnessShown = now;
            if (changed)
            {
                const auto area = loudnessArea();
                dirty = dirty.isEmpty() ? area : dirty.getUnion (area);
            }
        }
    if (! dirty.isEmpty())
    {
        meterRepaintOnly = meterCacheOn; // (the cached picture under the meters stays valid: they are drawn over it)
        repaint (dirty);
        meterRepaintOnly = false;
    }
    if (woken > 0) // (one per frame is enough: the next frame brings the next)
    {
        revealed.insert (woken);
        updateSize();
        repaint();
    }
    return moving;
}

// ---------------------------------------------------------------------------

namespace
{
constexpr int panelHeader = 30;
constexpr int inputRowHeight = 30; // the input row (arm, audio input source) above the slots
constexpr int slotsTop = panelHeader + inputRowHeight;
constexpr int slotHeight = 24;
const EffectKind allEffects[] = { EffectKind::eq,     EffectKind::parametricEq, EffectKind::facetEq, EffectKind::compressor, EffectKind::limiter, EffectKind::apexLimiter,
                                      EffectKind::delay,  EffectKind::reverb,       EffectKind::chorus,     EffectKind::filter,
                                      EffectKind::distortion, EffectKind::utility, EffectKind::volumeGate,
                                      EffectKind::pitchShift, EffectKind::freqShift, EffectKind::lofi, EffectKind::effector, EffectKind::crossover, EffectKind::frequencySplitter, EffectKind::autoPan, EffectKind::vocoder, EffectKind::pitchCorrect,
                                      EffectKind::ridgeStrip, EffectKind::glowLeveler, EffectKind::hushDeEsser, EffectKind::aether, EffectKind::ember, EffectKind::kilnChannel, EffectKind::fretChannel, EffectKind::shellChannel, EffectKind::strikeShaper, // LYNX's mixing suite
                                      EffectKind::lynxTune, // LYNX's own pitch corrector
                                      EffectKind::epochVerb, EffectKind::atrium, EffectKind::foilPlate, EffectKind::nebula, EffectKind::halo, EffectKind::horizon, // LYNX's reverbs
                                      EffectKind::echoform, EffectKind::spiralEcho, EffectKind::orbit, EffectKind::latticeMod, // LYNX's delay / modulation suite
                                      EffectKind::crestCompressor, EffectKind::prismMultiband, EffectKind::sentryGate, // LYNX's dynamics suite
                                      EffectKind::quellDenoiser, EffectKind::geminiDoubler, EffectKind::voiceShift, // LYNX's voice tools
                                      EffectKind::pocketDistortion, EffectKind::pocketLimiterClassic, EffectKind::pocketLimiter, EffectKind::pocketLeveller,
                                      EffectKind::pocketGate, EffectKind::pocketTranceDelay, EffectKind::pocketTapeDelay, EffectKind::pocketMultiFx, // Pocket: FL Studio Mobile's effects (round 188h)
                                      EffectKind::pocketAutoduck, EffectKind::pocketChorus, EffectKind::pocketSpacer, EffectKind::pocketStereoizer, // Pocket (round 189h)
                                      EffectKind::pocketWowFlutter, EffectKind::pocketPhaser, EffectKind::pocketFlanger, EffectKind::pocketTremolo, // Pocket (round 189h)
                                      EffectKind::pocketWaveshaper, EffectKind::pocketCompressor, EffectKind::pocketSpreader, // Pocket (round 189h)
                                      EffectKind::pocketComb, // Pocket (round 190by)
                                      EffectKind::pocketMultiband, // Pocket (round 190by)
                                      EffectKind::pocketTapeStop, // Pocket (round 190by)
                                      EffectKind::pocketEq, EffectKind::pocketGraphicEq, EffectKind::pocketFilter, // Pocket (round 190dw)
                                      EffectKind::pocketReverbClassic, EffectKind::pocketReverb2, EffectKind::pocketReverb, // Pocket (round 190dw)
                                      EffectKind::formulaController, EffectKind::peakController, EffectKind::xyController, EffectKind::xyzController,
                                      EffectKind::controlSurface, // controllers (link their output to any knob)
                                      EffectKind::patcher, // FL's Patcher: a graph of effects / instruments / plugins (REA_rodada141)
                                      EffectKind::hardwareInsert, // outboard gear through device outputs / inputs, pinged round trip (round 164d)
                                      EffectKind::fruityLimiter, EffectKind::fruityCompressor,
                                      EffectKind::fruityWaveShaper, EffectKind::fruitySoftClipper,
                                      EffectKind::reeverb2, EffectKind::reeverb1,
                                      EffectKind::fruityDelay2, EffectKind::fruityDelay3,
                                      EffectKind::stereoShaper, EffectKind::stereoEnhancer, EffectKind::transientProcessor,
                                      EffectKind::spreader, EffectKind::fruityChorus, EffectKind::hyperChorus, EffectKind::vintageChorus,
                                      EffectKind::fruityPanOMatic, EffectKind::fruityBalance, EffectKind::fruityBloodOverdrive, EffectKind::fruitySend,
                                      EffectKind::fruityDelayBank, EffectKind::fruityMultiband,
                                      EffectKind::fruityFastDist, EffectKind::fruityFlangus, EffectKind::fruityFlanger, EffectKind::fruityPhaser, EffectKind::luxeVerb,
                                      EffectKind::fruityFastLP, EffectKind::fruityFreeFilter, EffectKind::fruityFilter,
                                      EffectKind::hardcore,
                                      EffectKind::multibandDelay, EffectKind::emphasizer, EffectKind::flEmphasis, EffectKind::flFrequencyShifter, EffectKind::flLowLifter,
                                      EffectKind::flPhaseInverter, EffectKind::flCenter, EffectKind::flBassBoost, EffectKind::fl7BandEq, EffectKind::flDelay1, // round 165k
                                      EffectKind::waveCandy, EffectKind::visualizer, // visual plugins (round 164c)
                                      EffectKind::fruityMute2, EffectKind::bigClock, EffectKind::tuner, EffectKind::htmlNotebook, // round 165g
                                      EffectKind::scratcher, // round 165g (flscratch165): FL's turntable
                                      EffectKind::transmitter, // round 165g (fltrans165): FL's transient / sustain splitter
                                      EffectKind::flTransporter, // round 167f: FL's transient looper, exact
                                      // FL engines that need more than their parameters (round 142, issue #5): a new one starts from
                                      // the plugin's Default preset of the user's FL installation (core/NativeEngines.h)
                                      EffectKind::maximus, EffectKind::vocodex, EffectKind::equo, EffectKind::distructor, EffectKind::lovePhilter,
                                      EffectKind::grossBeat, EffectKind::convolver, EffectKind::flPitchShifter, EffectKind::vintagePhaser,
                                      EffectKind::squeeze, EffectKind::rhythmGain, EffectKind::newtone, EffectKind::newtime,
                                      EffectKind::edison }; // round 190cd: FL's audio editor as a slot plugin
} // namespace

const std::vector<EffectKind>& addableEffects()
{
    static const std::vector<EffectKind> list (std::begin (allEffects), std::end (allEffects));
    return list;
}

bool isFlEffect (EffectKind kind)
{
    // allEffects lists LYNX's own effects first and FL's from the Formula Controller on (the outboard gear insert is LYNX's)
    const auto& list = addableEffects();
    const auto first = std::find (list.begin(), list.end(), EffectKind::formulaController);
    const auto at = std::find (list.begin(), list.end(), kind);
    return at != list.end() && at >= first && kind != EffectKind::hardwareInsert;
}

bool addableEffectAvailable (EffectKind kind)
{
    const auto* e = natives::effectEngine (kind);
    return e == nullptr || natives::available (*e, flStudioAccess());
}

juce::String addableEffectName (EffectKind kind)
{
    const auto name = juce::String::fromUTF8 (effectInfo (kind).name);
    // LYNX's own pitch / frequency effects sit next to FL's look-alikes (Pitch Shifter, Frequency Shifter, Pitcher)
    // in the lists: tell them apart (round 162, issue #7 review)
    if (kind == EffectKind::pitchShift || kind == EffectKind::freqShift || kind == EffectKind::pitchCorrect)
        return tr ("{0} (LYNX SHARE)", { name });
    if (const auto* e = natives::effectEngine (kind))
    {
        if (! natives::available (*e, flStudioAccess()))
            return tr ("{0} (requires FL Studio)", { name });
        if (e->beta)
            return tr ("{0} (beta)", { name });
    }
    return name;
}

std::optional<EffectSlot> newAddableEffect (EffectKind kind, double bpm)
{
    return natives::newEffect (kind, flStudioAccess(), bpm);
}

EffectsPanel::EffectsPanel (AudioEngine& e)
    : engine (e)
{
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
    engine.addChangeListener (this);
    showingTimer = std::make_unique<ui::TimerWhileShowing> (*this, static_cast<juce::Timer&> (*this), 30); // (stopped while hidden; round 174f)
}

EffectsPanel::~EffectsPanel()
{
    engine.removeChangeListener (this);
}

juce::Rectangle<int> EffectsPanel::armBounds() const
{
    return { 8, panelHeader + 2, 24, inputRowHeight - 8 };
}

juce::Rectangle<int> EffectsPanel::inputBoxBounds() const
{
    return { 38, panelHeader + 2, getWidth() - 46, inputRowHeight - 8 };
}

void EffectsPanel::groupIntoRack (int slot, bool all)
{
    engine.capturePluginStates(); // (the plugins' current settings go with them)
    const auto t = track;
    const auto moved = all ? patcherize::slotsForAll (engine.getSong(), t) : std::vector<int> { slot };
    // a hosted plugin's automated parameter: its value now (node k is moved slot k), so its macro starts there
    auto values = [this, t, moved] (int node, int param) -> float {
        if (node < 0 || node >= (int) moved.size())
            return -1.0f;
        if (auto* p = dynamic_cast<PluginEffect*> (engine.getEffect (t, moved[(std::size_t) node]).get()); p != nullptr && ! p->hasCrashed())
            if (auto& instance = p->getInstance(); param >= 0 && param < instance.getParameters().size())
                return instance.getParameters()[param]->getValue();
        return -1.0f;
    };
    auto trial = engine.getSong();
    const auto at = patcherize::patcherizeSlots (trial, t, slot, all);
    if (at < 0 || ! rack::fromPatcher (trial.mixer[(std::size_t) t].effects[(std::size_t) at].patcher, values))
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, tr ("Group into Audio Effect Rack"),
                                                tr ("These effects can't go into a rack (a side-chain, or more than 16 automated parameters)."));
        return;
    }
    int placed = -1;
    engine.editSong ([t, slot, all, values, &placed] (Song& song) {
        placed = patcherize::patcherizeSlots (song, t, slot, all);
        if (placed >= 0)
            rack::fromPatcher (song.mixer[(std::size_t) t].effects[(std::size_t) placed].patcher, values);
    });
    if (placed >= 0)
        selectedSlot = placed;
    repaint();
}

void EffectsPanel::timerCallback()
{
    // the input level of the shown insert (the strips read their own); decays like a meter
    const auto& mixer = engine.getSong().mixer;
    const auto hasInput = track >= 0 && track < (int) mixer.size() && mixer[(std::size_t) track].inputChannel >= 0;
    auto changed = false;

    for (int side = 0; side < 2; ++side)
    {
        const auto peak = hasInput ? engine.peekInputPeak (track, side) : 0.0f;
        const auto level = juce::jmax (peak, inputLevel[(std::size_t) side] * 0.8f);
        changed = changed || std::abs (level - inputLevel[(std::size_t) side]) > 1e-4f;
        inputLevel[(std::size_t) side] = level < 1e-4f ? 0.0f : level;
    }

    if (changed && isShowing())
        repaint (inputBoxBounds());

    // another effect selected (its controls need another height): fold / unfold the EQ, scroll (round 164)
    if (const auto* s = slots(); s != nullptr && isShowing())
    {
        const auto kind = selectedSlot >= 0 && selectedSlot < (int) s->size() ? (int) (*s)[(std::size_t) selectedSlot].kind : -1;
        if (selectedSlot != fittedSlot || kind != fittedKind)
        {
            fittedSlot = selectedSlot;
            fittedKind = kind;
            refit();
        }
    }
}

void EffectsPanel::setTrack (int t)
{
    track = t;
    const auto* s = slots();
    selectedSlot = s != nullptr && ! s->empty() ? 0 : -1;
    refit();
    repaint();
}

const std::vector<EffectSlot>* EffectsPanel::slots() const
{
    const auto& mixer = engine.getSong().mixer;
    return track >= 0 && track < (int) mixer.size() ? &mixer[(std::size_t) track].effects : nullptr;
}

int EffectsPanel::rowOf (int index) const
{
    const auto* s = slots();
    if (s == nullptr || index < 0 || index >= (int) s->size())
        return -1;
    return mixerops::effectRows (*s)[(std::size_t) index];
}

int EffectsPanel::effectIn (int row) const
{
    const auto* s = slots();
    return s == nullptr ? -1 : mixerops::effectAtRow (*s, row);
}

juce::Rectangle<int> EffectsPanel::mixBounds (int slot) const
{
    return slotBounds (slot).withTrimmedRight (20).removeFromRight (24);
}

juce::Rectangle<int> EffectsPanel::ledBounds (int slot) const
{
    return slotBounds (slot).removeFromRight (20);
}

juce::Rectangle<int> EffectsPanel::arrowBounds (int slot) const
{
    return slotBounds (slot).removeFromLeft (18);
}

// --- the insert's built-in EQ and its output (FL's layout under the slots, REA round 104) ---------

namespace
{
constexpr int eqGraphHeight = 84, eqKnobsHeight = 46, outputRowHeight = 30;
constexpr float eqMinHz = 20.0f, eqMaxHz = 20000.0f, eqRangeDb = 18.0f;
const char* const eqBandNames[] = { TR_NOOP ("Low shelf"), TR_NOOP ("Peak"), TR_NOOP ("High shelf") };

juce::Colour eqBandColour (int band)
{
    return band == 0 ? juce::Colour (0xFFE0A33Au) : band == 1 ? juce::Colour (0xFF5FC27Eu) : juce::Colour (0xFF5B9BE6u);
}

// stored 0..1 <-> the band's value
float eqToValue (const TrackEq& eq, int band, int what)
{
    const auto b = (std::size_t) band;
    if (what == 0)
        return (eq.gainDb[b] + eqRangeDb) / (2.0f * eqRangeDb);
    if (what == 1)
        return std::log (eq.hz[b] / 10.0f) / std::log (1600.0f);
    return eq.width[b];
}

void eqFromValue (TrackEq& eq, int band, int what, float v)
{
    const auto b = (std::size_t) band;
    v = juce::jlimit (0.0f, 1.0f, v);
    if (what == 0)
        eq.gainDb[b] = std::round ((v * 2.0f * eqRangeDb - eqRangeDb) * 10.0f) / 10.0f;
    else if (what == 1)
        eq.hz[b] = std::round (10.0f * std::pow (1600.0f, v));
    else
        eq.width[b] = v;
}

// the EQ's response at `hz` (dB), with the filters the mixer runs (RackRenderer::applyTrackEq)
float eqResponseDb (const TrackEq& eq, double hz, double sampleRate)
{
    Biquad bands[3];
    bands[0].shelf (sampleRate, std::min ((double) eq.hz[0], sampleRate * 0.45), eq.gainDb[0], false, TrackEq::shelfSlope (eq.width[0]));
    bands[1].peaking (sampleRate, std::min ((double) eq.hz[1], sampleRate * 0.45), TrackEq::peakQ (eq.width[1]), eq.gainDb[1]);
    bands[2].shelf (sampleRate, std::min ((double) eq.hz[2], sampleRate * 0.45), eq.gainDb[2], true, TrackEq::shelfSlope (eq.width[2]));
    const auto w = juce::MathConstants<double>::twoPi * hz / sampleRate;
    const std::complex<double> z1 = std::polar (1.0, -w), z2 = std::polar (1.0, -2.0 * w);
    double db = 0.0;
    for (int b = 0; b < 3; ++b)
    {
        if (eq.gainDb[(std::size_t) b] == 0.0f)
            continue; // a band at 0 dB is not processed (like FL)
        const auto& f = bands[b];
        const auto h = (f.b0 + f.b1 * z1 + f.b2 * z2) / (1.0 + f.a1 * z1 + f.a2 * z2);
        db += 20.0 * std::log10 (std::max (1e-9, std::abs (h)));
    }
    return (float) db;
}
} // namespace

constexpr int eqFoldBarHeight = 20;

bool EffectsPanel::eqShown() const
{
    return ! engine.getSong().cleanMixer; // clean mixer (round 163): no built-in EQ to show
}

int EffectsPanel::belowHeight (bool folded) const
{
    if (! eqShown()) // clean mixer: only the output row
        return outputRowHeight + 8;
    return outputRowHeight + eqFoldBarHeight + (folded ? 4 : eqGraphHeight + eqKnobsHeight + 14);
}

int EffectsPanel::bottomTop() const
{
    return getHeight() - belowHeight (eqFolded);
}

juce::Rectangle<int> EffectsPanel::eqFoldBounds() const
{
    if (! eqShown())
        return {};
    return { 8, bottomTop(), getWidth() - 16, eqFoldBarHeight };
}

int EffectsPanel::neededHeight (bool folded) const
{
    // the slots and the selected effect's controls (round 164: its knobs and their names and values too, they were
    // cut by the EQ at medium heights): when they don't fit with the EQ open, it folds; then the panel scrolls
    return selectedEffectBottom() + 6 + belowHeight (folded);
}


void EffectsPanel::fitToView (int w, int h, int scrollBar)
{
    viewWidth = w;
    viewHeight = h;
    scrollBarThickness = scrollBar;
    refit();
}

void EffectsPanel::refit()
{
    if (viewWidth <= 0 || viewHeight <= 0)
        return;
    const auto folded = eqFoldChoice == 2 || (eqFoldChoice == 0 && viewHeight < neededHeight (false));
    const auto need = neededHeight (folded);
    const auto scrolls = viewHeight < need;
    const auto changed = folded != eqFolded;
    eqFolded = folded;
    const juce::Rectangle<int> size (viewWidth - (scrolls ? scrollBarThickness : 0), juce::jmax (viewHeight, need));
    if (getWidth() != size.getWidth() || getHeight() != size.getHeight())
        setSize (size.getWidth(), size.getHeight());
    else if (changed)
        repaint();
}

juce::Rectangle<int> EffectsPanel::eqGraphBounds() const
{
    return { 8, bottomTop() + eqFoldBarHeight, getWidth() - 16 - 3 * 18 - 6, eqGraphHeight };
}

juce::Rectangle<int> EffectsPanel::eqFaderBounds (int band) const
{
    const auto graph = eqGraphBounds();
    return { graph.getRight() + 6 + band * 18, graph.getY(), 16, graph.getHeight() };
}

juce::Rectangle<int> EffectsPanel::eqKnobBounds (int band, int what) const
{
    const auto fader = eqFaderBounds (band);
    return { fader.getCentreX() - 11, eqGraphBounds().getBottom() + 2 + (what - 1) * 22, 22, 22 };
}

juce::Rectangle<int> EffectsPanel::outputBounds() const
{
    return { 34, getHeight() - outputRowHeight - 2, getWidth() - 42, outputRowHeight - 8 };
}

juce::Point<float> EffectsPanel::eqHandle (int band) const
{
    const auto graph = eqGraphBounds().toFloat().reduced (4.0f);
    const auto& eq = engine.getSong().mixer[(std::size_t) track].eq;
    const auto hz = juce::jlimit (eqMinHz, eqMaxHz, eq.hz[(std::size_t) band]);
    const auto x = graph.getX() + graph.getWidth() * std::log (hz / eqMinHz) / std::log (eqMaxHz / eqMinHz);
    const auto y = graph.getCentreY() - graph.getHeight() * 0.5f * eq.gainDb[(std::size_t) band] / eqRangeDb;
    return { x, y };
}

int EffectsPanel::eqControlAt (juce::Point<int> p) const
{
    if (slots() == nullptr || eqFolded || ! eqShown())
        return 0;
    for (int b = 0; b < 3; ++b)
    {
        if (eqFaderBounds (b).contains (p))
            return eqParamBase - b * 3;
        for (int what = 1; what <= 2; ++what)
            if (eqKnobBounds (b, what).contains (p))
                return eqParamBase - b * 3 - what;
    }
    if (eqGraphBounds().contains (p))
    {
        // the nearest handle (by distance in the graph): FL's "Tweak band N"
        int best = 0;
        float bestDistance = 1e9f;
        for (int b = 0; b < 3; ++b)
            if (const auto d = eqHandle (b).getDistanceFrom (p.toFloat()); d < bestDistance)
            {
                bestDistance = d;
                best = b;
            }
        return eqPointBase - best;
    }
    return 0;
}

void EffectsPanel::setEqFromGraph (int band, juce::Point<int> p)
{
    const auto graph = eqGraphBounds().toFloat().reduced (4.0f);
    const auto fx = juce::jlimit (0.0f, 1.0f, (p.x - graph.getX()) / graph.getWidth());
    const auto hz = juce::jlimit (10.0f, 16000.0f, std::round (eqMinHz * std::pow (eqMaxHz / eqMinHz, fx)));
    const auto db = juce::jlimit (-eqRangeDb, eqRangeDb, std::round ((graph.getCentreY() - p.y) / (graph.getHeight() * 0.5f) * eqRangeDb * 10.0f) / 10.0f);
    engine.editSong ([t = track, band, hz, db] (Song& s) {
        if (t < (int) s.mixer.size())
        {
            s.mixer[(std::size_t) t].eq.hz[(std::size_t) band] = hz;
            s.mixer[(std::size_t) t].eq.gainDb[(std::size_t) band] = db;
        }
    });
}

void EffectsPanel::paintEq (juce::Graphics& g)
{
    using namespace theme;
    const auto& song = engine.getSong();
    if (track < 0 || track >= (int) song.mixer.size())
        return;
    const auto& eq = song.mixer[(std::size_t) track].eq;
    const auto top = bottomTop();
    g.setColour (colour::border);
    g.fillRect (8, top, getWidth() - 16, 1);

    // the title bar: click folds / unfolds the EQ (it folds by itself when the mixer is too short for it)
    if (eqShown())
    {
        const auto bar = eqFoldBounds().toFloat();
        const auto active = eq.active();
        auto chevron = bar.withWidth (14.0f).withSizeKeepingCentre (12.0f, 12.0f);
        {
            juce::Graphics::ScopedSaveState state (g);
            if (eqFolded)
                g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, chevron.getCentreX(), chevron.getCentreY()));
            icons::draw (g, Icon::chevronDown, chevron, colour::textSecondary);
        }
        g.setFont (font (type::small, true));
        g.setColour (colour::textSecondary);
        textcache::drawText (g, tr ("EQ"), bar.withTrimmedLeft (18.0f).toNearestInt(), juce::Justification::centredLeft);
        if (eq.bypassed && eq.hasBoostOrCut()) // round 177d: bypassed, its settings kept
        {
            g.setColour (colour::textDisabled);
            g.setFont (font (type::caption));
            textcache::drawText (g, tr ("bypassed"), bar.withTrimmedLeft (44.0f).toNearestInt(), juce::Justification::centredLeft);
        }
        else if (eqFolded && active) // a folded EQ that still changes the sound says so
        {
            g.setColour (colour::accent);
            g.setFont (font (type::caption));
            textcache::drawText (g, tr ("active"), bar.withTrimmedLeft (44.0f).toNearestInt(), juce::Justification::centredLeft);
        }
    }
    if (! eqFolded && eqShown())
    {
        // the response graph: frequency (log) across, ±18 dB up and down
        const auto graph = eqGraphBounds().toFloat();
        theme::fillRoundRect (g, graph, radius::sm, colour::bg1);
        g.setColour (colour::border);
        g.drawRoundedRectangle (graph.reduced (0.5f), radius::sm, 1.0f);
        const auto inner = graph.reduced (4.0f);
        g.setColour (colour::gridBeat);
        for (float hz : { 100.0f, 1000.0f, 10000.0f })
        {
            const auto x = inner.getX() + inner.getWidth() * std::log (hz / eqMinHz) / std::log (eqMaxHz / eqMinHz);
            g.drawVerticalLine ((int) x, inner.getY(), inner.getBottom());
        }
        g.drawHorizontalLine ((int) inner.getCentreY(), inner.getX(), inner.getRight());

        juce::Path curve;
        const auto sampleRate = juce::jmax (8000.0, engine.getTransport().sampleRate.load());
        for (int i = 0; i <= (int) inner.getWidth(); i += 2)
        {
            const auto hz = eqMinHz * std::pow (eqMaxHz / eqMinHz, (float) i / inner.getWidth());
            const auto db = juce::jlimit (-eqRangeDb, eqRangeDb, eqResponseDb (eq, hz, sampleRate));
            const auto y = inner.getCentreY() - inner.getHeight() * 0.5f * db / eqRangeDb;
            if (i == 0)
                curve.startNewSubPath (inner.getX(), y);
            else
                curve.lineTo (inner.getX() + (float) i, y);
        }
        g.setColour (eq.active() ? colour::accent : colour::textSecondary.withAlpha (0.6f));
        g.strokePath (curve, juce::PathStrokeType (1.5f));

        for (int b = 0; b < 3; ++b)
        {
            const auto hot = hoverEq == eqPointBase - b || dragParam == eqPointBase - b;
            const auto handle = eqHandle (b);
            g.setColour (eqBandColour (b).withAlpha (hot ? 1.0f : 0.8f));
            g.fillEllipse (juce::Rectangle<float> (hot ? 10.0f : 8.0f, hot ? 10.0f : 8.0f).withCentre (handle));
        }

        // the gain faders (one per band) and the frequency / width knobs under them
        for (int b = 0; b < 3; ++b)
        {
            const auto fader = eqFaderBounds (b).toFloat();
            const auto hot = hoverEq == eqParamBase - b * 3 || dragParam == eqParamBase - b * 3;
            theme::fillRoundRect (g, fader.withSizeKeepingCentre (3.0f, fader.getHeight() - 6.0f), 1.5f, colour::bg0);
            g.setColour (colour::border);
            g.drawHorizontalLine ((int) fader.getCentreY(), fader.getX() + 2.0f, fader.getRight() - 2.0f);
            const auto v = eqToValue (eq, b, 0);
            const auto y = fader.getBottom() - 6.0f - v * (fader.getHeight() - 12.0f);
            theme::fillRoundRect (g, juce::Rectangle<float> (fader.getWidth() - 2.0f, 7.0f).withCentre ({ fader.getCentreX(), y }), 2.0f, hot ? colour::textPrimary : colour::textSecondary);
            g.setColour (eqBandColour (b));
            g.fillRect (juce::Rectangle<float> (fader.getWidth() - 6.0f, 1.5f).withCentre ({ fader.getCentreX(), y }));

            for (int what = 1; what <= 2; ++what)
            {
                const auto code = eqParamBase - b * 3 - what;
                drawKnob (g, eqKnobBounds (b, what).toFloat().reduced (2.0f), eqToValue (eq, b, what), false, eqBandColour (b),
                          hoverEq == code || dragParam == code);
            }
        }

        // the knob captions take the width their (translated) text needs, the title gives way (round 165o)
        const juce::Font captionFont (font (type::caption));
        const auto captionWidth = juce::jlimit (32, 70, (int) std::ceil (juce::jmax (juce::GlyphArrangement::getStringWidth (captionFont, tr ("Freq")),
                                                                                     juce::GlyphArrangement::getStringWidth (captionFont, tr ("Width")))) + 2);
        g.setColour (colour::textSecondary);
        g.setFont (font (type::title));
        textcache::drawText (g, tr ("Equalizer"), 10, eqGraphBounds().getBottom() + 4, eqKnobBounds (0, 1).getX() - 8 - captionWidth, 20,
                             juce::Justification::centredLeft);
        g.setFont (captionFont);
        g.setColour (colour::textDisabled);
        textcache::drawText (g, tr ("Freq"), eqKnobBounds (0, 1).getX() - 2 - captionWidth, eqKnobBounds (0, 1).getY(), captionWidth, 22,
                             juce::Justification::centredRight);
        textcache::drawText (g, tr ("Width"), eqKnobBounds (0, 2).getX() - 2 - captionWidth, eqKnobBounds (0, 2).getY(), captionWidth, 22,
                             juce::Justification::centredRight);
    }

    // the audio output target
    auto box = outputBounds().toFloat();
    g.setColour (colour::border);
    g.fillRect (8, (int) box.getY() - 5, getWidth() - 16, 1);
    icons::draw (g, Icon::exportAudio, juce::Rectangle<float> (10.0f, box.getY(), 18.0f, box.getHeight()).withSizeKeepingCentre (16.0f, 16.0f),
                 colour::textSecondary);
    theme::fillRoundRect (g, box, radius::sm, hoverOutput ? colour::bg4 : colour::bg3);
    g.setColour (hoverOutput ? colour::borderStrong : colour::border);
    g.drawRoundedRectangle (box.reduced (0.5f), radius::sm, 1.0f);
    g.setColour (track == 0 ? colour::textPrimary : colour::textSecondary);
    g.setFont (font (type::body));
    textcache::drawText (g, outputName(), box.withTrimmedLeft (8.0f).withTrimmedRight (18.0f).toNearestInt(), juce::Justification::centredLeft, true);
    icons::draw (g, Icon::chevronDown, box.removeFromRight (18.0f).reduced (4.0f).translated (-2.0f, 0.0f), colour::textSecondary);
}

juce::String EffectsPanel::outputName() const
{
    if (track == 0 && const_cast<AudioEngine&> (engine).getDeviceManager().getCurrentAudioDevice() == nullptr)
        return tr ("(no audio device)");
    return mixeroutput::label (engine, track);
}

void EffectsPanel::showOutputMenu()
{
    mixeroutput::showMenu (engine, track, this, localAreaToGlobal (outputBounds()));
}

namespace mixeroptions
{
std::function<void()> changed;
std::function<void (bool)> setShowUsedOnly;
std::function<void (bool)> setShowLoudness;
std::function<void (std::vector<int>, std::function<void (const std::vector<juce::File>&)>)> renderTracks;

namespace
{
void notifyChanged()
{
    if (changed)
        changed();
}

void askNumber (const juce::String& title, const juce::String& text, int value, int lo, int hi, std::function<void (int)> done)
{
    auto* w = new juce::AlertWindow (title, text, juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("n", juce::String (value));
    w->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([w, lo, hi, done = std::move (done)] (int result) {
        if (result == 1)
            done (juce::jlimit (lo, hi, w->getTextEditorContents ("n").trim().getIntValue()));
    }), true);
}
} // namespace

void addItems (juce::PopupMenu& menu, AudioEngine& engine, bool newProjects)
{
    const auto& song = engine.getSong();
    menu.addItem (tr ("Clean mixer: no built-in EQ on the inserts (this project)"), true, song.cleanMixer, [&engine] {
        const auto on = ! engine.getSong().cleanMixer;
        engine.editSong ([on] (Song& s) { s.cleanMixer = on; });
        const auto& mixer = engine.getSong().mixer;
        const auto withEq = std::count_if (mixer.begin(), mixer.end(), [] (const MixerTrack& t) { return t.eq.active(); });
        if (on && withEq > 0) // nothing is lost: switching it off brings the EQ back as it was
            ui::postHint ({ tr ("Clean mixer: the EQ of {0} insert(s) is bypassed and hidden (kept in the project)", { juce::String ((int) withEq) }), {}, false, false }, 5000);
    });
    // LYNX option (round 190y): the old cheap mute; FL keeps a muted insert's effects running (and showing its level)
    menu.addItem (tr ("Muted inserts don't process (LYNX, saves CPU; this project)"), true, song.mutedInsertsSkip, [&engine] {
        const auto on = ! engine.getSong().mutedInsertsSkip;
        engine.editSong ([on] (Song& s) { s.mutedInsertsSkip = on; });
        if (on)
            ui::postHint ({ tr ("Muted inserts skip their effects: a Fruity Send or controller in one goes quiet, its meter stops, and its effects start fresh when unmuted (FL keeps them running)"), {}, false, false }, 6000);
    });
    menu.addItem (tr ("Show only used inserts"), true, MixerStrips::showUsedOnly, [] {
        const auto on = ! MixerStrips::showUsedOnly;
        if (setShowUsedOnly)
            setShowUsedOnly (on);
        else
            MixerStrips::showUsedOnly = on;
        notifyChanged();
    });
    // LYNX's own (round 190am; off = FL's mixer): the Master's loudness in LUFS and its true peak, beside the Master
    menu.addItem (tr ("Loudness meter beside the Master (LYNX)"), true, MixerStrips::showLoudness, [] {
        const auto on = ! MixerStrips::showLoudness;
        if (setShowLoudness)
            setShowLoudness (on);
        else
            MixerStrips::showLoudness = on;
        notifyChanged();
    });
    menu.addItem (tr ("Remove unused inserts at the end"), song.mixer.size() > 1 && ! song.insertInUse ((int) song.mixer.size() - 1), false, [&engine] {
        int removed = 0;
        engine.editSong ([&removed] (Song& s) { removed = mixerops::removeUnusedTrailingInserts (s); });
        ui::postHint ({ tr ("Removed {0} unused insert(s)", { juce::String (removed) }), {}, false, false }, 3000);
    });
    // LYNX's option (round 186c, pain point 1; off = FL, which drops them): a deleted insert's incoming sends stay greyed
    menu.addItem (tr ("Keep links to deleted inserts, greyed (LYNX)"), true, MixerStrips::keepLostLinks, [] {
        MixerStrips::keepLostLinks = ! MixerStrips::keepLostLinks;
        notifyChanged();
    });
    // LYNX's addition (round 186c, pain point 4; off by default): the mixer's groups mirror the Playlist's track groups
    menu.addItem (tr ("Mixer groups follow the Playlist (LYNX)"), true, MixerStrips::followPlaylist, [&engine] {
        MixerStrips::followPlaylist = ! MixerStrips::followPlaylist;
        if (MixerStrips::followPlaylist)
            engine.editSong ([] (Song& s) { mixerops::followPlaylistGroups (s); });
        notifyChanged();
    });
    menu.addItem (tr ("Arrange inserts like the Playlist"), ! mixerops::playlistOrderInserts (song).empty(), false, [&engine] {
        int moved = 0;
        engine.editSongStructural ([&moved] (Song& s) { moved = mixerops::arrangeLikePlaylist (s); });
        ui::postHint ({ tr ("{0} insert(s) moved to follow the Playlist", { juce::String (moved) }), {}, false, false }, 3000);
    });

    if (! newProjects)
        return;

    auto& d = newProjectDefaults();
    menu.addSectionHeader (tr ("New projects"));
    {
        juce::PopupMenu inserts;
        bool listed = false;
        for (int n : { 0, 1, 2, 4, 8, 12, 16, 24, 32, 48, 64, 100, 125 })
        {
            listed = listed || n == d.inserts;
            inserts.addItem (n == NewProjectDefaults::defaultInserts ? tr ("{0} (default)", { juce::String (n) }) : juce::String (n), true, n == d.inserts, [n] {
                newProjectDefaults().inserts = n;
                notifyChanged();
            });
        }
        inserts.addItem (listed ? tr ("Other...") : tr ("Other ({0})...", { juce::String (d.inserts) }), true, ! listed, [] {
            askNumber (tr ("Inserts in new projects"),
                       tr ("How many mixer inserts a new project starts with (0 to {0}). More are added when you need them.", { juce::String (maxMixerTracks - 1) }),
                       newProjectDefaults().inserts, 0, maxMixerTracks - 1, [] (int n) {
                           newProjectDefaults().inserts = n;
                           notifyChanged();
                       });
        });
        menu.addSubMenu (tr ("Inserts in new projects ({0})", { juce::String (d.inserts) }), inserts);
    }
    {
        juce::PopupMenu chain;
        const std::pair<const char*, MasterChain> chains[] = { { TR_NOOP ("Empty (nothing on the Master)"), MasterChain::empty },
                                                               { TR_NOOP ("Soft clipper only"), MasterChain::clipper },
                                                               { TR_NOOP ("LYNX SHARE default (limiter)"), MasterChain::lynxDefault } };
        for (const auto& [label, c] : chains)
            chain.addItem (tr (label), true, d.master == c, [c = c] {
                newProjectDefaults().master = c;
                notifyChanged();
            });
        menu.addSubMenu (tr ("Master chain in new projects"), chain);
    }
    menu.addItem (tr ("Clean mixer in new projects (no built-in EQ)"), true, d.cleanMixer, [] {
        newProjectDefaults().cleanMixer = ! newProjectDefaults().cleanMixer;
        notifyChanged();
    });
    {
        juce::PopupMenu tracks;
        for (int n : { 4, 8, 16, 24, 32, 64, 100 })
            tracks.addItem (n == 16 ? tr ("{0} (default)", { juce::String (n) }) : juce::String (n), true, n == d.playlistTracks, [n] {
                newProjectDefaults().playlistTracks = n;
                notifyChanged();
            });
        menu.addSubMenu (tr ("Playlist tracks shown ({0})", { juce::String (d.playlistTracks) }), tracks);
    }
}
} // namespace mixeroptions

namespace mixeroutput
{
namespace
{
juce::String modeSuffix (OutputMode mode)
{
    switch (mode)
    {
        case OutputMode::stereo: return {};
        case OutputMode::monoSum: return " " + tr ("(mono)");
        case OutputMode::monoLeft: return " " + tr ("(mono, L)");
        case OutputMode::monoRight: return " " + tr ("(mono, R)");
    }
    return {};
}
} // namespace

bool isCustom (const AudioEngine& engine, int track)
{
    const auto& mixer = engine.getSong().mixer;
    if (track < 0 || track >= (int) mixer.size())
        return false;
    const auto channel = mixer[(std::size_t) track].outputChannel;
    return track == 0 ? channel != outputDefault : channel >= 0;
}

juce::String label (const AudioEngine& engine, int track, bool compact)
{
    const auto& mixer = engine.getSong().mixer;
    if (track < 0 || track >= (int) mixer.size())
        return {};

    const auto& mt = mixer[(std::size_t) track];
    auto channel = mt.outputChannel;
    auto mode = mt.outputMode;
    if (track == 0 && channel == outputDefault)
        channel = 0, mode = OutputMode::stereo;
    if (channel < 0)
        return tr ("(none)");

    const auto names = engine.getOutputChannelNames();
    const auto missing = ! outputAvailable (channel, mode, (int) names.size())
                         || ! engine.isOutputChannelOpen (channel) || (mode == OutputMode::stereo && ! engine.isOutputChannelOpen (channel + 1));

    if (compact) // the strip badge: the channel numbers only
    {
        auto text = juce::String::fromUTF8 ("\xe2\x86\x92 ") + juce::String (channel + 1);
        if (mode == OutputMode::stereo)
            text << "-" << (channel + 2);
        else
            text << (mode == OutputMode::monoLeft ? " L" : mode == OutputMode::monoRight ? " R" : " m");
        return missing ? text + " ?" : text;
    }

    auto nameOf = [&] (int c) {
        return c < (int) names.size() ? juce::String::fromUTF8 (names[(std::size_t) c].c_str()) : tr ("Out {0}", { juce::String (c + 1) });
    };
    auto text = mode == OutputMode::stereo ? nameOf (channel) + " - " + nameOf (channel + 1) : nameOf (channel);
    text += modeSuffix (mode);
    if (missing)
        text += " " + tr ("(missing)");
    return text;
}

void showMenu (AudioEngine& engine, int track, juce::Component* target, juce::Rectangle<int> screenArea)
{
    const auto& mixer = engine.getSong().mixer;
    if (track < 0 || track >= (int) mixer.size())
        return;

    const auto& mt = mixer[(std::size_t) track];
    const auto names = engine.getOutputChannelNames();
    const auto custom = isCustom (engine, track);
    juce::PopupMenu menu;
    menu.addSectionHeader (tr ("Audio output target"));

    auto set = [&engine, track] (int channel, OutputMode mode) {
        return [&engine, track, channel, mode] { engine.setTrackOutput (track, channel, mode); };
    };

    if (track == 0)
    {
        const auto first = names.size() >= 2 ? juce::String::fromUTF8 (names[0].c_str()) + " - " + juce::String::fromUTF8 (names[1].c_str())
                                             : tr ("first output pair");
        menu.addItem (first + "  " + tr ("(default)"), true, mt.outputChannel == outputDefault, set (outputDefault, OutputMode::stereo));
        menu.addItem (tr ("(none)"), true, mt.outputChannel == outputNone, set (outputNone, OutputMode::stereo));
    }
    else
        menu.addItem (tr ("(none) - through the Master only"), true, ! custom, set (outputDefault, OutputMode::stereo));

    if (custom && (track > 0 || mt.outputChannel >= 0))
        menu.addItem (label (engine, track), true, true, nullptr); // the current target (also when missing)

    // the optional direct outputs: stereo pairs, then every channel alone
    juce::PopupMenu direct;
    direct.addItem (track == 0 ? tr ("Optional: send the Master to other outputs of your audio interface")
                               : tr ("Optional: also send this insert straight to an output of your audio interface, e.g. a mono output"),
                    false, false, nullptr);
    if (names.empty())
        direct.addItem (tr ("No audio device is open"), false, false, nullptr);

    const auto targets = outputTargets (names);
    auto pairsHeader = false, monoHeader = false;
    for (const auto& t : targets)
    {
        const auto name = juce::String::fromUTF8 (t.name.c_str());
        if (t.mode == OutputMode::stereo)
        {
            if (! std::exchange (pairsHeader, true))
                direct.addSectionHeader (tr ("Stereo pair"));
            direct.addItem (name, true, mt.outputChannel == t.channel && mt.outputMode == OutputMode::stereo, set (t.channel, OutputMode::stereo));
            continue;
        }

        if (! std::exchange (monoHeader, true))
            direct.addSectionHeader (tr ("Mono (one output)"));
        const auto here = mt.outputChannel == t.channel && mt.outputMode != OutputMode::stereo;
        juce::PopupMenu modes;
        modes.addItem (tr ("Sum of L + R (mono)"), true, here && mt.outputMode == OutputMode::monoSum, set (t.channel, OutputMode::monoSum));
        modes.addItem (tr ("Left side only"), true, here && mt.outputMode == OutputMode::monoLeft, set (t.channel, OutputMode::monoLeft));
        modes.addItem (tr ("Right side only"), true, here && mt.outputMode == OutputMode::monoRight, set (t.channel, OutputMode::monoRight));
        direct.addSubMenu (name, modes, true, nullptr, here);
    }

    if (! names.empty() && names.size() <= 2)
        direct.addItem (tr ("This audio device has 2 outputs; an interface with more outputs lists all of them here"), false, false, nullptr);

    menu.addSeparator();
    menu.addSubMenu (track == 0 ? tr ("Hardware output") : tr ("Send to hardware output (optional)"), direct, true, nullptr, custom && mt.outputChannel >= 0);
    if (custom)
        menu.addItem (track == 0 ? tr ("Back to the default output") : tr ("Back to Master only"), set (outputDefault, OutputMode::stereo));

    menu.addSeparator();
    menu.addItem (tr ("Audio settings..."), [] {
        if (mixerinput::openAudioSettings)
            mixerinput::openAudioSettings();
    });

    auto options = juce::PopupMenu::Options();
    if (target != nullptr)
        options = options.withTargetComponent (target);
    if (! screenArea.isEmpty())
        options = options.withTargetScreenArea (screenArea);
    else if (target != nullptr)
        options = options.withMousePosition();
    menu.showMenuAsync (options);
}
} // namespace mixeroutput

juce::Rectangle<int> EffectsPanel::slotBounds (int slot) const
{
    return { 8, slotsTop + slot * slotHeight, getWidth() - 16, slotHeight - 3 };
}

namespace
{
// parameters shown as knobs: in the rhythmic envelope, only those of the points in use
int shownParams (const EffectSlot& slot)
{
    const auto all = (int) effectInfo (slot.kind).params.size();
    if (slot.kind == EffectKind::rhythmGain)
        return std::min (all, 2 + 3 * (int) std::lround (slot.params[1] * 12.0f));
    if (slot.kind == EffectKind::lynxTune) // the note switches are the keyboard strip above the knobs
        return std::min (all, (int) lynxtune::noteC);
    return all;
}
} // namespace

juce::Rectangle<int> EffectsPanel::knobBounds (int param) const
{
    return knobBoundsIn (param, bottomTop());
}

juce::Rectangle<int> EffectsPanel::knobBoundsIn (int param, int areaBottom) const
{
    auto top = slotsTop + maxEffectSlots * slotHeight + 40;
    // many parameters (Effector: 12): 4 columns and shorter rows to fit the height
    int count = 0;
    if (const auto* s = slots(); ! gateSelected() && s != nullptr && selectedSlot >= 0 && selectedSlot < (int) s->size())
        count = shownParams ((*s)[(std::size_t) selectedSlot]);
    const auto columns = count > 9 ? 4 : 3;
    const auto w = (getWidth() - 16) / columns;
    const auto rows = juce::jmax (1, (count + columns - 1) / columns);
    const auto rowHeight = count > 9 ? juce::jlimit (64, 86, (areaBottom - top - 4) / rows) : 86;

    // volume gate: the 32 steps are bars (gateStepsBounds); the knobs are mix/attack/release
    if (gateSelected())
    {
        if (param < 32)
            return {};
        param -= 32;
        top = gateStepsBounds().getBottom() + 22;
    }
    if (vocoderSelected()) // the band levels above the knobs
        top = bandLevelsBounds().getBottom() + 22;
    if (tuneSelected()) // LYNX Tune's note keyboard above the knobs
        top = tuneKeysBounds().getBottom() + 22;

    return { 8 + (param % columns) * w, top + (param / columns) * rowHeight, w, rowHeight - 4 };
}

int EffectsPanel::selectedEffectBottom() const
{
    const auto slotsBottom = slotsTop + maxEffectSlots * slotHeight;
    auto bottom = slotsBottom + 64; // the effect's title row
    const auto* s = slots();
    if (s == nullptr || selectedSlot < 0 || selectedSlot >= (int) s->size())
        return bottom;
    const auto& slot = (*s)[(std::size_t) selectedSlot];
    if (slot.kind == EffectKind::plugin || isSoundgoodizerApprox (slot))
        return juce::jmax (bottom, pluginButtonBounds().getBottom() + 34 + 40);
    if (slot.kind == EffectKind::parametricEq || slot.kind == EffectKind::facetEq)
        return juce::jmax (bottom, pluginButtonBounds().getBottom() + 10 + 120);
    if (slot.kind == EffectKind::parallel)
        return juce::jmax (bottom, knobBoundsIn (0, 0).getY() - 28 + 20 + 22 * (int) slot.branches.size());
    const auto count = gateSelected() ? 35 : shownParams (slot);
    for (int p = 0; p < count; ++p)
        if (const auto r = knobBoundsIn (p, 0); ! r.isEmpty()) // (tight grids at their shortest rows)
            bottom = juce::jmax (bottom, r.getY() + juce::jlimit (30, 44, r.getHeight() - 30) + 32); // knob, name, value
    return bottom;
}

bool EffectsPanel::gateSelected() const
{
    const auto* s = slots();
    return s != nullptr && selectedSlot >= 0 && selectedSlot < (int) s->size() && (*s)[(std::size_t) selectedSlot].kind == EffectKind::volumeGate;
}

juce::Rectangle<int> EffectsPanel::gateStepsBounds() const
{
    return { 10, slotsTop + maxEffectSlots * slotHeight + 40, getWidth() - 20, 90 };
}

void EffectsPanel::setGateStep (juce::Point<int> p)
{
    const auto area = gateStepsBounds();
    const auto step = juce::jlimit (0, 31, (int) ((p.x - area.getX()) * 32 / juce::jmax (1, area.getWidth())));
    const auto value = juce::jlimit (0.0f, 1.0f, (float) (area.getBottom() - p.y) / (float) area.getHeight());
    engine.editSong ([t = track, slot = selectedSlot, step, value] (Song& song) {
        song.mixer[(std::size_t) t].effects[(std::size_t) slot].params[(std::size_t) step] = value;
    });
}

bool EffectsPanel::vocoderSelected() const
{
    const auto* s = slots();
    return s != nullptr && selectedSlot >= 0 && selectedSlot < (int) s->size() && (*s)[(std::size_t) selectedSlot].kind == EffectKind::vocoder;
}

bool EffectsPanel::tuneSelected() const
{
    const auto* s = slots();
    return s != nullptr && selectedSlot >= 0 && selectedSlot < (int) s->size() && (*s)[(std::size_t) selectedSlot].kind == EffectKind::lynxTune;
}

juce::Rectangle<int> EffectsPanel::tuneKeysBounds() const
{
    return { 10, slotsTop + maxEffectSlots * slotHeight + 40, getWidth() - 20, 54 };
}

namespace
{
// LYNX Tune's keyboard strip: one octave, white keys in a row, black keys on top (C … B → key rectangles)
juce::Rectangle<float> tuneKeyRect (juce::Rectangle<float> area, int pc)
{
    static constexpr int whiteIndex[12] { 0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6 };
    static constexpr float blackAt[12] { 0, 1.0f, 0, 2.0f, 0, 0, 4.0f, 0, 5.0f, 0, 6.0f, 0 };
    const auto w = area.getWidth() / 7.0f;
    if (whiteIndex[pc] >= 0)
        return { area.getX() + whiteIndex[pc] * w, area.getY(), w, area.getHeight() };
    return { area.getX() + blackAt[pc] * w - w * 0.3f, area.getY(), w * 0.6f, area.getHeight() * 0.58f };
}

int tuneKeyAt (juce::Rectangle<float> area, juce::Point<float> p)
{
    for (int pc = 0; pc < 12; ++pc) // black keys first: they lie on top
        if ((pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10) && tuneKeyRect (area, pc).contains (p))
            return pc;
    for (int pc = 0; pc < 12; ++pc)
        if (tuneKeyRect (area, pc).contains (p))
            return pc;
    return -1;
}
} // namespace

void EffectsPanel::toggleTuneNote (juce::Point<int> p)
{
    const auto pc = tuneKeyAt (tuneKeysBounds().toFloat(), p.toFloat());
    if (pc < 0)
        return;
    engine.editSong ([t = track, slot = selectedSlot, pc] (Song& song) {
        auto& v = song.mixer[(std::size_t) t].effects[(std::size_t) slot].params[(std::size_t) (lynxtune::noteC + pc)];
        v = v >= 0.5f ? 0.0f : 1.0f;
    });
}

juce::Rectangle<int> EffectsPanel::bandLevelsBounds() const
{
    return { 10, slotsTop + maxEffectSlots * slotHeight + 40, getWidth() - 20, 70 };
}

namespace
{
// the Fruity Vocoder's state for the slot's current band count (levels kept while the count matches, as FL does)
fvocoder::State vocoderState (const EffectSlot& slot)
{
    auto s = fvocoder::parse (slot.state).value_or (fvocoder::State());
    const auto bands = 4 << std::clamp ((int) std::lround (slot.params[0] * 5.0f), 0, 5);
    if (s.bands != bands)
    {
        s.bands = bands;
        s.levels.fill (1.0f);
    }
    s.order = 1 + std::clamp ((int) std::lround (slot.params[1] * 2.0f), 0, 2);
    s.listen = std::clamp ((int) std::lround (slot.params[2] * 2.0f), 0, 2);
    s.swap = slot.params[3] > 0.5f;
    for (int c = 0; c < fvocoder::controls; ++c)
        s.c[(std::size_t) c] = fvocoder::rawFromKnob (slot.params[(std::size_t) (4 + c)], c, s.c[(std::size_t) c]);
    return s;
}

constexpr float maxBandLevel = 2.0f; // the strip spans 0…2 (1 = unchanged)
} // namespace

void EffectsPanel::setBandLevel (juce::Point<int> p)
{
    const auto* s = slots();
    if (s == nullptr || ! vocoderSelected())
        return;
    const auto st = vocoderState ((*s)[(std::size_t) selectedSlot]);
    const auto area = bandLevelsBounds();
    const auto band = juce::jlimit (0, st.bands - 1, (int) ((p.x - area.getX()) * st.bands / juce::jmax (1, area.getWidth())));
    auto level = juce::jlimit (0.0f, maxBandLevel, maxBandLevel * (float) (area.getBottom() - p.y) / (float) area.getHeight());
    if (std::abs (level - 1.0f) < 0.04f)
        level = 1.0f; // snaps to the unchanged level
    engine.editSong ([t = track, slot = selectedSlot, band, level] (Song& song) {
        auto& fx = song.mixer[(std::size_t) t].effects[(std::size_t) slot];
        auto state = vocoderState (fx);
        state.levels[(std::size_t) band] = level;
        fx.state = fvocoder::serialise (state);
    });
}

void EffectsPanel::showBandLevelMenu()
{
    juce::PopupMenu menu;
    auto apply = [this] (float (*level) (int, int)) {
        engine.editSong ([t = track, slot = selectedSlot, level] (Song& song) {
            auto& fx = song.mixer[(std::size_t) t].effects[(std::size_t) slot];
            auto state = vocoderState (fx);
            for (int k = 0; k < state.bands; ++k)
                state.levels[(std::size_t) k] = level (k, state.bands);
            fx.state = fvocoder::serialise (state);
        });
    };
    menu.addItem (tr ("Reset band levels"), [apply] { apply ([] (int, int) { return 1.0f; }); });
    menu.addItem (tr ("Brighter (tilt up)"), [apply] { apply ([] (int k, int n) { return 0.5f + (float) k / (float) std::max (1, n - 1); }); });
    menu.addItem (tr ("Darker (tilt down)"), [apply] { apply ([] (int k, int n) { return 1.5f - (float) k / (float) std::max (1, n - 1); }); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

void EffectsPanel::showGatePatterns()
{
    // ready-made patterns (like Gross Beat's slots): value of step i of 32 (1/8 beat each)
    struct Pattern
    {
        const char* name;
        float (*level) (int);
    };
    static const Pattern patterns[] = {
        { TR_NOOP ("All open"), [] (int) { return 1.0f; } },
        { TR_NOOP ("1/2 beat gate"), [] (int i) { return (i % 4) < 2 ? 1.0f : 0.0f; } },
        { TR_NOOP ("1 beat gate"), [] (int i) { return (i % 8) < 4 ? 1.0f : 0.0f; } },
        { TR_NOOP ("Offbeat"), [] (int i) { return (i % 8) >= 4 ? 1.0f : 0.0f; } },
        { TR_NOOP ("Sidechain (ramp per beat)"), [] (int i) { return std::min (1.0f, 0.15f + (float) (i % 8) / 5.0f); } },
        { TR_NOOP ("Trance gate"), [] (int i) { return (i % 4) == 3 ? 0.0f : (i % 16) >= 12 ? 0.4f : 1.0f; } },
        { TR_NOOP ("Fade-in over the bar"), [] (int i) { return (float) (i + 1) / 32.0f; } },
        { TR_NOOP ("Fade-out over the bar"), [] (int i) { return 1.0f - (float) i / 32.0f; } },
    };

    juce::PopupMenu menu;
    for (const auto& p : patterns)
        menu.addItem (tr (p.name), [this, level = p.level] {
            engine.editSong ([t = track, slot = selectedSlot, level] (Song& song) {
                auto& params = song.mixer[(std::size_t) t].effects[(std::size_t) slot].params;
                for (int i = 0; i < 32; ++i)
                    params[(std::size_t) i] = level (i);
            });
        });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

int EffectsPanel::knobAt (juce::Point<int> p) const
{
    const auto* s = slots();

    if (s == nullptr || selectedSlot < 0 || selectedSlot >= (int) s->size())
        return -1;

    if ((*s)[(std::size_t) selectedSlot].kind == EffectKind::parametricEq || (*s)[(std::size_t) selectedSlot].kind == EffectKind::facetEq)
        return -1; // edited in its own window

    const auto count = shownParams ((*s)[(std::size_t) selectedSlot]);

    for (int i = gateSelected() ? 32 : 0; i < count; ++i)
        if (knobBounds (i).contains (p))
            return i;

    return -1;
}

void EffectsPanel::paint (juce::Graphics& g)
{
    const uitrace::Scope trace ("EffectsPanel paint", true, g.getClipBounds());
    using namespace theme;
    g.fillAll (colour::bg2);
    g.setColour (colour::border);
    g.drawVerticalLine (0, 0.0f, (float) getHeight());

    const auto* s = slots();

    if (s == nullptr)
        return;

    const auto& song = engine.getSong();
    g.setColour (colour::textPrimary);
    g.setFont (font (type::title, true));
    textcache::drawText (g, tr ("Effects: {0}", { juce::String::fromUTF8 (song.mixer[(std::size_t) track].name.c_str()) }), 10, 4,
                getWidth() - 70, panelHeader - 8, juce::Justification::centredLeft, true);

    // input row: arm switch (inserts) and the audio input source with its level (the Master has one too, as in FL;
    // REA round 143)
    if (track >= 0 && track < (int) song.mixer.size())
    {
        const auto& mt = song.mixer[(std::size_t) track];
        if (track > 0)
        {
            const auto armed = engine.isInsertArmed (track);
            const auto a = armBounds().toFloat();
            theme::fillRoundRect (g, a, radius::sm, armed ? colour::record.withAlpha (0.25f) : hoverInput == 1 ? colour::bg5 : colour::bg3);
            const auto dot = a.withSizeKeepingCentre (11.0f, 11.0f);
            g.setColour (armed ? colour::record : colour::bg0);
            g.fillEllipse (dot);
            g.setColour (armed ? colour::record.brighter (0.3f) : hoverInput == 1 ? colour::textSecondary : colour::borderStrong);
            g.drawEllipse (dot, 1.0f);
            if (mt.recordSafe)
            {
                g.setColour (colour::textDisabled);
                g.drawLine (dot.getX() + 1.0f, dot.getBottom() - 1.0f, dot.getRight() - 1.0f, dot.getY() + 1.0f, 1.2f);
            }
        }

        auto box = inputBoxBounds().toFloat();
        theme::fillRoundRect (g, box, radius::sm, hoverInput == 2 ? colour::bg4 : colour::bg3);
        g.setColour (hoverInput == 2 ? colour::borderStrong : colour::border);
        g.drawRoundedRectangle (box.reduced (0.5f), radius::sm, 1.0f);

        // the level, as a thin bar along the bottom of the box (left over right)
        if (mt.inputChannel >= 0)
        {
            const auto bar = box.reduced (4.0f, 0.0f).withTop (box.getBottom() - 5.0f).withHeight (3.0f);
            g.setColour (colour::bg0);
            g.fillRect (bar);
            for (int side = 0; side < 2; ++side)
            {
                const auto db = juce::Decibels::gainToDecibels (inputLevel[(std::size_t) side], -60.0f);
                const auto fill = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
                g.setColour (inputLevel[(std::size_t) side] >= 0.99f ? colour::record : colour::meterLow);
                g.fillRect (bar.withHeight (1.5f).translated (0.0f, side * 1.5f).withWidth (bar.getWidth() * fill));
            }
        }

        icons::draw (g, Icon::mic, box.withWidth (18.0f).reduced (2.0f).translated (3.0f, -1.0f), mt.inputChannel >= 0 ? colour::textPrimary : colour::textDisabled);
        const auto names = engine.getInputChannelNames();
        const auto label = mt.inputChannel >= 0 ? juce::String::fromUTF8 (inputSourceName (mt.inputChannel, mt.inputStereo, names).c_str())
                                                : tr ("No audio input");
        g.setColour (mt.inputChannel >= 0 ? colour::textPrimary : colour::textSecondary);
        g.setFont (font (type::body));
        textcache::drawText (g, label, box.withTrimmedLeft (24.0f).withTrimmedRight (18.0f).translated (0.0f, -1.0f).toNearestInt(), juce::Justification::centredLeft, true);
        icons::draw (g, Icon::chevronDown, box.removeFromRight (18.0f).reduced (4.0f).translated (-2.0f, -1.0f), colour::textSecondary);
    }

    // the insert's effects switch
    if (! s->empty())
    {
        const auto on = mixerops::anyEffectEnabled (song, track);
        const auto r = fxSwitchBounds().toFloat();
        theme::fillRoundRect (g, r, radius::sm, on ? colour::bg5 : colour::bg0);
        g.setColour (muteLed (! on));
        g.fillEllipse (r.withWidth (r.getHeight()).reduced (6.0f).translated (3.0f, 0.0f));
        g.setColour (on ? colour::textPrimary : colour::textDisabled);
        g.setFont (font (type::caption, true));
        textcache::drawText (g, "FX", r.withTrimmedLeft (r.getHeight()).toNearestInt(), juce::Justification::centredLeft);
    }

    g.setFont (font (type::body));

    // the 10 slots, as FL draws them: options arrow, name ("Slot N" when empty), mix knob, LED
    // (rows: FL's slot places, an effect may sit below empty slots, round 170b)
    for (int row = 0; row < maxEffectSlots; ++row)
    {
        const auto i = effectIn (row); // the effect shown here, -1 = empty
        const auto r = slotBounds (row);
        const auto filled = i >= 0;
        const auto isSelected = i == selectedSlot && filled;
        const auto slotHovered = row == hoverSlot && dragSlot < 0;
        theme::fillRoundRect (g, r.toFloat(), radius::sm, isSelected ? colour::bg5 : filled ? (slotHovered ? colour::bg4 : colour::bg3) : (slotHovered ? colour::bg3 : colour::bg2.brighter (0.03f)));
        g.setColour (slotHovered ? colour::borderStrong : colour::border);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), radius::sm, 1.0f);

        if (isSelected)
        {
            theme::fillRoundRect (g, r.toFloat().withWidth (3.0f).reduced (0.0f, 3.0f), 1.5f, colour::accent);
        }

        {
            const auto a = arrowBounds (row).toFloat().withSizeKeepingCentre (5.0f, 8.0f).translated (1.0f, 0.0f);
            juce::Path tri;
            tri.addTriangle (a.getX(), a.getY(), a.getRight(), a.getCentreY(), a.getX(), a.getBottom());
            g.setColour (slotHovered && hoverArrow ? colour::textPrimary : colour::textSecondary.withAlpha (filled ? 1.0f : 0.6f));
            g.fillPath (tri);
        }

        const auto ledArea = ledBounds (row).toFloat().withSizeKeepingCentre (8.0f, 8.0f);
        const auto textArea = r.withTrimmedLeft (20).withTrimmedRight (46);

        if (filled)
        {
            const auto& slot = (*s)[(std::size_t) i];
            // "Low latency while recording" (round 190aq): a slot it bypasses now keeps its LED's place as an amber
            // ring, its name dims and a badge says why; the slot's own on / off state is untouched
            const auto lowLatency = engine.isSlotLowLatencyBypassed (track, i);
            if (lowLatency)
            {
                g.setColour (colour::warning);
                g.drawEllipse (ledArea.reduced (0.75f), 1.5f);
            }
            else
            {
                g.setColour (muteLed (! slot.enabled));
                g.fillEllipse (ledArea);
            }
            if (slotHovered && hoverLed)
            {
                g.setColour (colour::textSecondary);
                g.drawEllipse (ledArea.expanded (2.0f), 1.0f);
            }
            g.setColour (slot.enabled ? colour::textPrimary : colour::textDisabled);
            // a LYNX substitute shows the plugin it stands in for, with a "LYNX" badge
            const auto substitute = slot.isSubstitute();
            auto name = quickpick::slotName (engine, slot); // (a plugin renamed in the Plugin database: its new name, round 165c)
            if (slot.look != 0) // part of a Master look (round 189k, the Simple workflow's Finish dock)
                name << " " << tr ("(look)");
            if (goals::active (slot)) // a one-knob effect of the Simple workflow's dock (round 190bu): its goal's name too
                name << " (" << (slot.goal == goals::punch ? trc ("@one-knob effect: Punch") : tr (goals::name (slot.goal))) << ")";
            if (slot.sidechain > 0 && slot.sidechain < (int) engine.getSong().mixer.size())
                name << juce::String::fromUTF8 (" \xe2\x86\x90 ") << juce::String::fromUTF8 (engine.getSong().mixer[(std::size_t) slot.sidechain].name.c_str());
            auto text = textArea;
            if (substitute)
            {
                const auto badge = text.removeFromRight (34).withSizeKeepingCentre (32, 14).toFloat();
                const auto playing = engine.getSong().slotActive (slot); // off while the project's substitutes are bypassed
                theme::fillRoundRect (g, badge, 3.0f, colour::info.withAlpha (playing ? 0.25f : 0.12f));
                g.setColour (playing ? colour::info : colour::textDisabled);
                const auto keep = g.getCurrentFont();
                g.setFont (font (type::caption, true));
                textcache::drawText (g, "LYNX", badge, juce::Justification::centred);
                g.setFont (keep);
                g.setColour (slot.enabled ? colour::textPrimary : colour::textDisabled);
            }
            if (lowLatency)
            {
                const auto label = tr ("LOW LAT");
                const auto keep = g.getCurrentFont();
                g.setFont (font (type::caption, true));
                const auto width = juce::jmin (text.getWidth() / 2, juce::jmax (30, juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), label) + 8));
                const auto badge = text.removeFromRight (width + 2).withSizeKeepingCentre (width, 14).toFloat();
                theme::fillRoundRect (g, badge, 3.0f, colour::warning.withAlpha (0.22f));
                g.setColour (colour::warning);
                textcache::drawText (g, label, badge, juce::Justification::centred);
                g.setFont (keep);
                g.setColour (colour::textDisabled);
            }
            textcache::drawText (g, name, text, juce::Justification::centredLeft);

            // slot's dry/wet knob
            const auto mixActive = (hoverParam == mixParam && hoverSlot == row) || (dragParam == mixParam && selectedSlot == i);
            drawKnob (g, mixBounds (row).toFloat().withSizeKeepingCentre (18.0f, 18.0f), slot.mix, false,
                      slot.enabled ? (dragParam == mixParam && selectedSlot == i ? colour::accent : colour::info) : colour::textDisabled, mixActive);
        }
        else
        {
            // an empty slot keeps its controls in place (dimmed), as FL shows them
            g.setColour (muteLed (false).withAlpha (0.3f));
            g.fillEllipse (ledArea);
            drawKnob (g, mixBounds (row).toFloat().withSizeKeepingCentre (18.0f, 18.0f), 1.0f, false, colour::textDisabled.withAlpha (0.5f), false);
            g.setColour (slotHovered ? colour::textSecondary : colour::textDisabled);
            g.setFont (juce::Font (font (type::body)).italicised());
            textcache::drawText (g, tr ("Slot {0}", { juce::String (row + 1) }), textArea, juce::Justification::centredLeft);
            g.setFont (font (type::body));
        }
    }

    paintEq (g);
    g.reduceClipRegion (0, 0, getWidth(), bottomTop() - 6); // the selected effect's knobs stay above the EQ

    if (dragSlot >= 0 && dropSlot >= 0 && dropSlot != rowOf (dragSlot)) // where the dragged slot lands (FL: swaps with
    {                                                                       // the effect there, or takes the empty slot)
        g.setColour (colour::accent);
        g.drawRoundedRectangle (slotBounds (dropSlot).toFloat().reduced (0.5f), radius::sm, 2.0f);
    }

    if (selectedSlot < 0 || selectedSlot >= (int) s->size())
        return;

    const auto& slot = (*s)[(std::size_t) selectedSlot];

    auto drawButton = [&g] (juce::Rectangle<int> button, const juce::String& text, Icon icon) {
        theme::fillRoundRect (g, button.toFloat(), radius::md, colour::bg4);
        const auto f = juce::Font (font (type::body));
        const auto textWidth = juce::GlyphArrangement::getStringWidth (f, text);
        auto content = button.toFloat().withSizeKeepingCentre (juce::jmin ((float) button.getWidth() - 12.0f, textWidth + 22.0f), (float) button.getHeight());
        icons::draw (g, icon, content.removeFromLeft (16.0f).withSizeKeepingCentre (16.0f, 16.0f), colour::textSecondary);
        content.removeFromLeft (6.0f);
        g.setColour (colour::textPrimary);
        g.setFont (f);
        textcache::drawFittedText (g, text, content.toNearestInt(), juce::Justification::centredLeft, 1, 0.85f);
    };

    if (slot.kind == EffectKind::plugin)
    {
        const auto button = pluginButtonBounds();
        drawButton (button, tr ("Open plugin interface"), Icon::plugin);
        g.setColour (colour::textSecondary);
        g.setFont (font (type::caption));
        textcache::drawText (g, quickpick::slotName (engine, slot), button.translated (0, 34), juce::Justification::centred);
        paintSidechainButton (g, slot);
        return;
    }

    if (slot.kind == EffectKind::parametricEq)
    {
        const auto button = pluginButtonBounds();
        drawButton (button, tr ("Open parametric EQ"), Icon::settings);
        const auto curve = juce::Rectangle<int> (button.getX(), button.getBottom() + 10, button.getWidth(), 120).toFloat();
        drawParametricEqCurve (g, curve, slot.params, juce::jmax (8000.0, engine.getTransport().sampleRate.load()), slot.enabled);
        return;
    }

    if (isSoundgoodizerApprox (slot)) // approximated without FL's preset files: its page edits Preset and Amount
    {
        const auto button = pluginButtonBounds();
        drawButton (button, tr ("Open Soundgoodizer"), Icon::settings);
        g.setColour (colour::textSecondary);
        g.setFont (font (type::caption));
        textcache::drawFittedText (g, tr ("Approximation (FL Studio's Soundgoodizer presets were not found on this computer)"),
                          button.translated (0, 34).withHeight (40), juce::Justification::centredTop, 2);
        return;
    }

    if (slot.kind == EffectKind::facetEq)
    {
        const auto button = pluginButtonBounds();
        drawButton (button, tr ("Open Facet EQ"), Icon::settings);
        const auto curve = juce::Rectangle<int> (button.getX(), button.getBottom() + 10, button.getWidth(), 120).toFloat();
        drawFacetEqCurve (g, curve, slot.params, juce::jmax (8000.0, engine.getTransport().sampleRate.load()), slot.enabled);
        return;
    }

    if (slot.kind == EffectKind::parallel) // container: list the branches
    {
        auto y = knobBounds (0).getY() - 28;
        g.setColour (colour::textPrimary);
        g.setFont (font (type::title, true));
        textcache::drawText (g, juce::String::fromUTF8 (effectInfo (slot.kind).name), 10, y, getWidth() - 20, 20, juce::Justification::centredLeft);
        g.setFont (font (type::small));
        for (std::size_t b = 0; b < slot.branches.size(); ++b)
        {
            y += 22;
            juce::String chain;
            if (slot.branches[b].chain.empty())
                chain = tr ("dry");
            for (std::size_t c = 0; c < slot.branches[b].chain.size(); ++c)
                chain += (c > 0 ? " -> " : "") + juce::String::fromUTF8 (effectInfo (slot.branches[b].chain[c].kind).name);
            const auto line = tr ("Branch {0} (x{1}): {2}", { juce::String ((int) b + 1), juce::String (slot.branches[b].gain, 2), chain });
            g.setColour (colour::textSecondary);
            textcache::drawText (g, line, 14, y, getWidth() - 28, 20, juce::Justification::centredLeft, true);
        }
        return;
    }

    const auto& info = effectInfo (slot.kind);
    const auto gate = slot.kind == EffectKind::volumeGate;
    const auto vocoder = slot.kind == EffectKind::vocoder;
    g.setColour (colour::textPrimary);
    g.setFont (font (type::title, true));
    const auto tune = slot.kind == EffectKind::lynxTune;
    const auto editorButton = ! gate && ! vocoder && NativeEffectWindows::canOpen (slot.kind);
    const auto titleY = (gate ? gateStepsBounds().getY() : vocoder ? bandLevelsBounds().getY() : tune ? tuneKeysBounds().getY() : knobBounds (0).getY()) - 28;
    const auto scButton = sidechainButtonBounds();
    const auto titleRight = scButton.isEmpty() ? getWidth() - 10 - (editorButton ? editorButtonBounds().getWidth() + 6 : 0) : scButton.getX() - 6;
    textcache::drawText (g, juce::String::fromUTF8 (info.name), 10, titleY, titleRight - 10, 20, juce::Justification::centredLeft, true);
    if (slot.isSubstitute()) // which missing plugin this stands in for (the project keeps it)
    {
        const auto titleWidth = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), juce::String::fromUTF8 (info.name));
        g.setColour (colour::info);
        g.setFont (font (type::caption));
        const auto original = juce::String::fromUTF8 (substituteOriginalName (slot).c_str());
        textcache::drawText (g, engine.getSong().substitutesBypassed ? tr ("LYNX SHARE substitute for {0} (bypassed)", { original }) : tr ("LYNX SHARE substitute for {0}", { original }),
                    10 + titleWidth + 10, titleY,
                    juce::jmax (0, titleRight - 20 - titleWidth), 20, juce::Justification::centredLeft, true);
    }

    if (editorButton) // the effect's own window (FL's plugin window)
    {
        const auto b = editorButtonBounds().toFloat();
        const auto hover = getMouseXYRelative().toFloat();
        theme::fillRoundRect (g, b, radius::md, b.contains (hover) ? colour::bg5 : colour::bg4);
        icons::draw (g, Icon::plugin, b.withWidth (22.0f).withSizeKeepingCentre (13.0f, 13.0f).translated (4.0f, 0.0f), colour::textSecondary);
        g.setColour (colour::textPrimary);
        g.setFont (font (type::small));
        textcache::drawFittedText (g, tr ("Editor"), b.withTrimmedLeft (24.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
    }
    paintSidechainButton (g, slot);

    if (tune) // the scale's notes on one octave; a note switched off is never a target
    {
        const auto area = tuneKeysBounds().toFloat();
        const auto scaleMask = vocal::scaleMask ((vocal::Scale) lynxtune::scaleIndex (slot.params[lynxtune::scale]), lynxtune::keyIndex (slot.params[lynxtune::key]), 0xfff);
        const auto custom = lynxtune::scaleIndex (slot.params[lynxtune::scale]) == (int) vocal::Scale::custom;
        for (const auto black : { false, true })
            for (int pc = 0; pc < 12; ++pc)
            {
                const auto isBlack = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
                if (isBlack != black)
                    continue;
                const auto r = tuneKeyRect (area, pc).reduced (1.0f);
                const auto on = slot.params[(std::size_t) (lynxtune::noteC + pc)] >= 0.5f;
                const auto inScale = custom || (scaleMask & (1u << pc)) != 0;
                auto fill = isBlack ? colour::bg0 : colour::bg2;
                if (inScale && on)
                    fill = (slot.enabled ? colour::accent : colour::textDisabled).withAlpha (isBlack ? 0.85f : 0.65f);
                theme::fillRoundRect (g, r, 2.0f, fill);
                g.setColour (colour::gridBar);
                g.drawRoundedRectangle (r, 2.0f, 1.0f);
                if (inScale && ! on) // in the scale but switched off
                {
                    g.setColour (colour::textSecondary);
                    g.drawLine (r.getX() + 3.0f, r.getBottom() - 3.0f, r.getRight() - 3.0f, r.getBottom() - 14.0f, 1.2f);
                }
                if (! isBlack)
                {
                    static const char* names[12] { "C", "", "D", "", "E", "F", "", "G", "", "A", "", "B" };
                    g.setColour (inScale && on ? colour::textPrimary : colour::textSecondary);
                    g.setFont (font (type::caption));
                    textcache::drawText (g, names[pc], r.withTrimmedTop (r.getHeight() - 16.0f), juce::Justification::centred);
                }
            }
        g.setColour (colour::textSecondary);
        g.setFont (font (type::caption));
        textcache::drawFittedText (g, tr ("Target notes: click a key to switch it off or on"), tuneKeysBounds().withY (tuneKeysBounds().getBottom() + 2).withHeight (14),
                          juce::Justification::centredLeft, 1, 0.8f);
    }

    if (vocoder) // the analysis bands' levels, low to high (FL's band level strip)
    {
        const auto st = vocoderState (slot);
        const auto area = bandLevelsBounds().toFloat();
        theme::fillRoundRect (g, area, radius::sm, colour::bg0);
        const auto w = area.getWidth() / (float) st.bands;
        const auto unity = area.getBottom() - area.getHeight() / maxBandLevel;
        for (int k = 0; k < st.bands; ++k)
        {
            const auto h = area.getHeight() * juce::jlimit (0.0f, 1.0f, st.levels[(std::size_t) k] / maxBandLevel);
            g.setColour ((slot.enabled ? colour::accent : colour::textDisabled).withAlpha (k % 2 == 0 ? 1.0f : 0.8f));
            g.fillRect (area.getX() + (float) k * w + (w > 4.0f ? 1.0f : 0.0f), area.getBottom() - h, juce::jmax (1.0f, w - (w > 4.0f ? 2.0f : 0.0f)), h);
        }
        g.setColour (colour::gridBar);
        g.drawHorizontalLine ((int) unity, area.getX(), area.getRight());
        g.setColour (colour::textSecondary);
        g.setFont (font (type::caption));
        textcache::drawFittedText (g, tr ("Band levels: draw them; right-click = presets"), area.withY (area.getBottom() + 2).withHeight (14).toNearestInt(),
                          juce::Justification::centredLeft, 1, 0.8f);
    }

    if (gate) // 32 steps = 1 bar of 4 beats
    {
        const auto area = gateStepsBounds().toFloat();
        theme::fillRoundRect (g, area, radius::sm, colour::bg0);
        const auto w = area.getWidth() / 32.0f;
        for (int i = 0; i < 32; ++i)
        {
            const auto h = area.getHeight() * juce::jlimit (0.0f, 1.0f, slot.params[(std::size_t) i]);
            g.setColour ((slot.enabled ? colour::accent : colour::textDisabled).withAlpha ((i / 8) % 2 == 0 ? 1.0f : 0.7f));
            g.fillRect (area.getX() + i * w + 1.0f, area.getBottom() - h, w - 2.0f, h);
        }
        g.setColour (colour::gridBar);
        for (int beat = 1; beat < 4; ++beat)
            g.drawVerticalLine ((int) (area.getX() + beat * 8 * w), area.getY(), area.getBottom());
        g.setColour (colour::textSecondary);
        g.setFont (font (type::caption));
        textcache::drawFittedText (g, tr ("1 bar: draw the steps; right-click = patterns"), area.withY (area.getBottom() + 2).withHeight (14).toNearestInt(),
                          juce::Justification::centredLeft, 1, 0.8f);
    }

    for (int p = gate ? 32 : 0; p < shownParams (slot); ++p)
    {
        const auto r = knobBounds (p);
        const auto value = slot.params[(std::size_t) p];
        // 38 px knob; in tight grids (Effector) it shrinks to fit name and value below
        const auto knobHeight = juce::jlimit (30, 44, r.getHeight() - 30);
        drawKnob (g, r.withHeight (knobHeight).toFloat(), value, false, slot.enabled ? colour::knobArc : colour::textDisabled,
                  dragParam == p || (hoverParam == p && hoverSlot < 0));

        g.setColour (colour::textSecondary);
        g.setFont (font (type::caption));
        textcache::drawFittedText (g, juce::String::fromUTF8 (info.params[(std::size_t) p].name), r.getX(), r.getY() + knobHeight + 2, r.getWidth(), 14,
                          juce::Justification::centred, 1, 0.8f);
        g.setColour (colour::textPrimary);
        textcache::drawText (g, juce::String::fromUTF8 (formatEffectParam (slot.kind, p, value, song.bpm, slot.params).c_str()), r.getX(), r.getY() + knobHeight + 16,
                    r.getWidth(), 14, juce::Justification::centred);
    }
}

void EffectsPanel::mouseDown (const juce::MouseEvent& e)
{
    const auto* s = slots();

    if (s == nullptr)
        return;

    if (const auto sc = sidechainButtonBounds(); ! sc.isEmpty() && sc.contains (e.getPosition()))
    {
        pluginheader::showSidechainPicker (engine, track, selectedSlot, this, sc);
        return;
    }

    // round 177d (LYNX's own; FL's insert EQ has no bypass): right-click the EQ's title bar to bypass it, its settings kept
    if (eqShown() && eqFoldBounds().contains (e.getPosition()) && flmods::popup (e.mods) && track >= 0 && track < (int) engine.getSong().mixer.size())
    {
        const auto bypassed = engine.getSong().mixer[(std::size_t) track].eq.bypassed;
        juce::PopupMenu menu;
        menu.addItem (tr ("Bypass the EQ (its settings are kept)"), true, bypassed, [this, t = track] {
            engine.editSong ([t] (Song& song) {
                if (t >= 0 && t < (int) song.mixer.size())
                    song.mixer[(std::size_t) t].eq.bypassed = ! song.mixer[(std::size_t) t].eq.bypassed;
            });
            repaint();
        });
        menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
        return;
    }
    if (eqFoldBounds().contains (e.getPosition()) && e.mods.isLeftButtonDown())
    {
        eqFoldChoice = eqFolded ? 1 : 2;
        refit();
        repaint();
        return;
    }

    // the built-in EQ and the output target (bottom section)
    if (outputBounds().contains (e.getPosition()))
    {
        showOutputMenu();
        return;
    }

    if (const auto eqControl = eqControlAt (e.getPosition()); eqControl != 0)
    {
        if (isEqPoint (eqControl))
        {
            dragParam = eqControl;
            setEqFromGraph (eqPointBase - eqControl, e.getPosition());
            return;
        }
        if (flmods::popup (e.mods))
            ui::showValueMenu (*this, &engine, specFor (eqControl, -1), valueOf (eqControl, -1), [this, eqControl] (double v) { setValue (eqControl, -1, v); });
        else if (ui::isResetClick (e))
            setValue (eqControl, -1, specFor (eqControl, -1).defaultValue);
        else
        {
            dragParam = eqControl;
            drag.begin (e, valueOf (eqControl, -1));
            showTip (eqControl, -1, false);
        }
        return;
    }

    if (track > 0 && armBounds().contains (e.getPosition()))
    {
        if (flmods::popup (e.mods)) // FL: "Record <insert> to" (round 167a)
            mixerinput::chooseRecordTo (engine, track);
        else
            mixerinput::toggleArm (engine, track, this);
        repaint();
        return;
    }

    if (track >= 0 && inputBoxBounds().contains (e.getPosition()))
    {
        mixerinput::showMenu (engine, track, this);
        return;
    }

    if (! s->empty() && fxSwitchBounds().contains (e.getPosition()))
    {
        if (flmods::popup (e.mods))
            toggleAllWindows();
        else
        {
            const auto on = ! mixerops::anyEffectEnabled (engine.getSong(), track);
            engine.editSong ([t = track, on] (Song& song) { mixerops::setAllEffectsEnabled (song, t, on); });
        }
        return;
    }

    for (int row = 0; row < maxEffectSlots; ++row)
    {
        const auto r = slotBounds (row);

        if (! r.contains (e.getPosition()))
            continue;

        const auto i = effectIn (row);
        if (i < 0)
        {
            showAddMenu (row); // (FL: the plugin lands in this slot)
            return;
        }

        if (arrowBounds (row).contains (e.getPosition())) // FL's slot options arrow
        {
            selectedSlot = i;
            repaint();
            showSlotMenu (i);
            return;
        }

        selectedSlot = i;
        repaint();

        if (mixBounds (row).contains (e.getPosition()))
        {
            if (flmods::popup (e.mods))
                ui::showValueMenu (*this, &engine, specFor (mixParam, i), valueOf (mixParam, i), [this, i] (double v) { setValue (mixParam, i, v); });
            else if (ui::isResetClick (e))
            {
                setValue (mixParam, i, 1.0);
                showTip (mixParam, i, true);
            }
            else
            {
                dragParam = mixParam;
                dragStart = (*s)[(std::size_t) i].mix;
                drag.begin (e, dragStart);
                showTip (mixParam, i, false);
            }
            return;
        }

        if (flmods::popup (e.mods) || (e.mods.isShiftDown() && ! ledBounds (row).contains (e.getPosition()))) // (FL: Shift+click = the menu)
            showSlotMenu (i);
        else if (ledBounds (row).contains (e.getPosition())) // LED: bypass
            engine.editSong ([t = track, i] (Song& song) {
                auto& slot = song.mixer[(std::size_t) t].effects[(std::size_t) i];
                slot.enabled = ! slot.enabled;
            });
        else
            dragSlot = i; // dragging the name moves the effect to another slot (a click shows its window)
        return;
    }

    if (selectedSlot >= 0 && selectedSlot < (int) s->size() && (*s)[(std::size_t) selectedSlot].kind == EffectKind::plugin
        && pluginButtonBounds().contains (e.getPosition()))
    {
        openPluginWindow (selectedSlot);
        return;
    }

    if (selectedSlot >= 0 && selectedSlot < (int) s->size() && (*s)[(std::size_t) selectedSlot].kind == EffectKind::parametricEq
        && pluginButtonBounds().withHeight (170).contains (e.getPosition()))
    {
        ParametricEqWindows::open (engine, track, selectedSlot);
        return;
    }

    if (selectedSlot >= 0 && selectedSlot < (int) s->size() && isSoundgoodizerApprox ((*s)[(std::size_t) selectedSlot])
        && pluginButtonBounds().contains (e.getPosition()))
    {
        NativeEffectWindows::open (engine, track, selectedSlot);
        return;
    }

    if (selectedSlot >= 0 && selectedSlot < (int) s->size() && (*s)[(std::size_t) selectedSlot].kind == EffectKind::facetEq
        && pluginButtonBounds().withHeight (170).contains (e.getPosition()))
    {
        FacetEqWindows::open (engine, track, selectedSlot);
        return;
    }

    if (selectedSlot >= 0 && selectedSlot < (int) s->size() && (*s)[(std::size_t) selectedSlot].kind != EffectKind::volumeGate
        && ! vocoderSelected() && NativeEffectWindows::canOpen ((*s)[(std::size_t) selectedSlot].kind)
        && editorButtonBounds().contains (e.getPosition()))
    {
        openEditor (selectedSlot);
        return;
    }

    if (vocoderSelected() && bandLevelsBounds().contains (e.getPosition()))
    {
        if (flmods::popup (e.mods))
            showBandLevelMenu();
        else
        {
            dragParam = bandLevelParam;
            setBandLevel (e.getPosition());
        }
        return;
    }

    if (tuneSelected() && tuneKeysBounds().contains (e.getPosition()))
    {
        toggleTuneNote (e.getPosition());
        return;
    }

    if (gateSelected() && gateStepsBounds().contains (e.getPosition()))
    {
        if (flmods::popup (e.mods))
            showGatePatterns();
        else
        {
            dragParam = gateStepParam;
            setGateStep (e.getPosition());
        }
        return;
    }

    dragParam = knobAt (e.getPosition());

    if (dragParam >= 0 && flmods::popup (e.mods))
    {
        const auto p = dragParam;
        ui::showValueMenu (*this, &engine, specFor (p, selectedSlot), valueOf (p, selectedSlot),
                           [this, p, slot = selectedSlot] (double v) { setValue (p, slot, v); });
        dragParam = -1;
        return;
    }

    if (dragParam >= 0 && ui::isResetClick (e))
    {
        setValue (dragParam, selectedSlot, specFor (dragParam, selectedSlot).defaultValue);
        showTip (dragParam, selectedSlot, true);
        dragParam = -1;
        return;
    }

    if (dragParam >= 0)
    {
        dragStart = (*s)[(std::size_t) selectedSlot].params[(std::size_t) dragParam];
        drag.begin (e, dragStart);
        showTip (dragParam, selectedSlot, false);
    }
}

ui::ValueSpec EffectsPanel::specFor (int param, int slot) const
{
    const auto& song = engine.getSong();
    ui::ValueSpec spec;
    spec.pixelsForRange = 150.0;

    if (isEqParam (param)) // the built-in EQ (stored 0..1 here: gain ±18 dB, frequency 10 Hz..16 kHz log, width)
    {
        const auto band = (eqParamBase - param) / 3, what = (eqParamBase - param) % 3;
        const juce::String whatName = what == 0 ? tr ("level") : what == 1 ? tr ("frequency") : tr ("width");
        spec.name = juce::String::fromUTF8 (song.mixer[(std::size_t) track].name.c_str()) + " - " + tr ("EQ band {0} ({1})", { juce::String (band + 1), tr (eqBandNames[band]) })
                    + " " + whatName;
        TrackEq defaults;
        spec.defaultValue = eqToValue (defaults, band, what);
        spec.toText = [band, what] (double v) {
            TrackEq eq;
            eqFromValue (eq, band, what, (float) v);
            const auto b = (std::size_t) band;
            return what == 0 ? juce::String (eq.gainDb[b], 1) + " dB"
                   : what == 1 ? (eq.hz[b] >= 1000.0f ? juce::String (eq.hz[b] / 1000.0f, 2) + " kHz" : juce::String ((int) eq.hz[b]) + " Hz")
                               : juce::String (eq.width[b], 2);
        };
        spec.fromText = ui::inverseOf (spec.toText, 0.0, 1.0);
        return spec;
    }

    if (const auto* s = slots(); s == nullptr || slot < 0 || slot >= (int) s->size())
        return spec;

    const auto& fx = (*slots())[(std::size_t) slot];
    const auto& info = effectInfo (fx.kind);
    const auto trackName = juce::String::fromUTF8 (song.mixer[(std::size_t) track].name.c_str());

    if (param == mixParam)
    {
        spec = ui::percentSpec (trackName + " - " + utf8 (info.name) + " - " + tr ("Mix"), 1.0);
        spec.target = AutomationTarget { AutomationTargetKind::effectMix, track, slot };
        return spec;
    }

    if (param < 0 || param >= (int) info.params.size())
        return spec;

    spec.name = trackName + " - " + utf8 (info.name) + " - " + utf8 (info.params[(std::size_t) param].name);
    spec.defaultValue = info.params[(std::size_t) param].defaultValue;
    spec.toText = [kind = fx.kind, param, bpm = song.bpm, context = fx.params] (double v) { return utf8 (formatEffectParam (kind, param, (float) v, bpm, context).c_str()); };
    spec.fromText = ui::inverseOf (spec.toText, 0.0, 1.0);
    spec.target = AutomationTarget { AutomationTargetKind::effectParam, track, slot, param };
    return spec;
}

double EffectsPanel::valueOf (int param, int slot) const
{
    if (isEqParam (param))
        return track >= 0 && track < (int) engine.getSong().mixer.size()
                   ? eqToValue (engine.getSong().mixer[(std::size_t) track].eq, (eqParamBase - param) / 3, (eqParamBase - param) % 3)
                   : 0.0;

    const auto* s = slots();

    if (s == nullptr || slot < 0 || slot >= (int) s->size())
        return 0.0;

    const auto& fx = (*s)[(std::size_t) slot];
    return param == mixParam ? fx.mix : (param >= 0 && param < (int) fx.params.size() ? fx.params[(std::size_t) param] : 0.0f);
}

void EffectsPanel::setValue (int param, int slot, double value)
{
    if (isEqParam (param))
    {
        engine.editSong ([t = track, band = (eqParamBase - param) / 3, what = (eqParamBase - param) % 3, v = (float) value] (Song& song) {
            if (t < (int) song.mixer.size())
                eqFromValue (song.mixer[(std::size_t) t].eq, band, what, v);
        });
        return;
    }

    const auto* s = slots();

    if (s == nullptr || slot < 0 || slot >= (int) s->size())
        return;

    if (param == mixParam)
        engine.editSong ([t = track, slot, v = (float) value] (Song& song) { song.mixer[(std::size_t) t].effects[(std::size_t) slot].mix = v; });
    else if (param >= 0)
        engine.editSong ([t = track, slot, param, v = (float) value] (Song& song) {
            song.mixer[(std::size_t) t].effects[(std::size_t) slot].params[(std::size_t) param] = v;
        });
}

int EffectsPanel::mixKnobAt (juce::Point<int> p) const
{
    for (int i = 0; slots() != nullptr && i < (int) slots()->size(); ++i)
        if (mixBounds (rowOf (i)).contains (p))
            return i;

    return -1;
}

void EffectsPanel::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    // a spin that is scrolling the mixer goes on scrolling over the EQ, the knobs and the slot names (round 169c)
    if (ui::wheelScrolling())
    {
        ui::noteWheelScroll();
        juce::Component::mouseWheelMove (e, wheel);
        return;
    }

    if (auto eqControl = eqControlAt (e.getPosition()); eqControl != 0)
    {
        if (isEqPoint (eqControl)) // the wheel over a handle changes the band's width
            eqControl = eqParamBase - (eqPointBase - eqControl) * 3 - 2;
        if (const auto v = ui::wheelValue (valueOf (eqControl, -1), e, wheel, specFor (eqControl, -1)))
        {
            setValue (eqControl, -1, *v);
            showTip (eqControl, -1, true);
        }
        return;
    }

    if (const auto slot = mixKnobAt (e.getPosition()); slot >= 0)
    {
        if (const auto v = ui::wheelValue (valueOf (mixParam, slot), e, wheel, specFor (mixParam, slot)))
        {
            setValue (mixParam, slot, *v);
            showTip (mixParam, slot, true);
        }
        return;
    }

    if (const auto p = knobAt (e.getPosition()); p >= 0)
    {
        if (const auto v = ui::wheelValue (valueOf (p, selectedSlot), e, wheel, specFor (p, selectedSlot)))
        {
            setValue (p, selectedSlot, *v);
            showTip (p, selectedSlot, true);
        }
        return;
    }

    // FL (REA FUN_012ab750): the wheel over a slot moves its effect one slot up or down (into an empty slot too,
    // wrapping around the 10 slots) and the mouse pointer follows it. A trackpad's small steps add up to one move.
    if (const auto slot = slotAt (e.getPosition()); slot >= 0)
    {
        const auto delta = (wheel.isReversed ? -1.0 : 1.0) * (wheel.deltaY != 0.0f ? wheel.deltaY : -wheel.deltaX);
        if (wheelSlot != slot || juce::Time::getMillisecondCounter() - wheelTime > 400)
            wheelAccumulator = 0.0;
        wheelSlot = slot;
        wheelTime = juce::Time::getMillisecondCounter();
        const auto steps = controls::slotWheelSteps (wheelAccumulator, delta, wheel.isSmooth, wheel.isInertial);
        if (steps == 0)
            return;
        int now = -1;
        engine.editSongStructural ([t = track, slot, steps, &now] (Song& song) { now = mixerops::moveEffectBy (song, t, slot, steps > 0 ? -1 : 1); });
        if (now >= 0)
        {
            selectedSlot = wheelSlot = now;
            const auto target = slotBounds (rowOf (now));
            juce::Desktop::getInstance().getMainMouseSource().setScreenPosition (localPointToGlobal (juce::Point<int> (e.x, target.getCentreY())).toFloat());
        }
        repaint();
        return;
    }

    ui::noteWheelScroll();
    juce::Component::mouseWheelMove (e, wheel);
}

ui::Hint EffectsPanel::hintAt (juce::Point<int> p)
{
    const auto gesture = ui::valueGestures();

    if (isEqParam (dragParam))
        return ui::valueHint (specFor (dragParam, -1), valueOf (dragParam, -1), gesture);

    if (const auto eqControl = eqControlAt (p); eqControl != 0)
    {
        if (isEqParam (eqControl))
            return ui::valueHint (specFor (eqControl, -1), valueOf (eqControl, -1), gesture);
        ui::Hint h;
        const auto band = eqPointBase - eqControl;
        h.text = tr ("EQ: [LMB] drag the band's frequency and level  [Wheel] width  [2xLMB] back to 0 dB");
        h.value = tr ("Band {0}", { juce::String (band + 1) }) + " - " + tr (eqBandNames[band]);
        return h;
    }

    if (slots() != nullptr && eqFoldBounds().contains (p))
    {
        ui::Hint h;
        h.text = (eqFolded ? tr ("Insert EQ (folded): [LMB] unfold it") : tr ("Insert EQ: [LMB] fold it (it folds by itself when the mixer is short)"))
                 + "  " + tr ("[RMB] bypass");
        return h;
    }

    if (outputBounds().contains (p))
    {
        ui::Hint h;
        h.text = track == 0 ? tr ("Audio output target: the outputs of your audio device the Master plays on  [LMB] choose")
                            : tr ("Audio output target: through the Master by default; optional: also straight to an output of your audio interface (a mono output too)  [LMB] choose");
        h.value = outputName();
        h.hasMenu = true;
        return h;
    }

    if (dragParam == mixParam || (dragParam >= 0 && drag.isActive()))
        return ui::valueHint (specFor (dragParam, dragParam == mixParam ? juce::jmax (0, selectedSlot) : selectedSlot),
                              valueOf (dragParam, dragParam == mixParam ? juce::jmax (0, selectedSlot) : selectedSlot), gesture);

    if (const auto slot = mixKnobAt (p); slot >= 0)
        return ui::valueHint (specFor (mixParam, slot), valueOf (mixParam, slot), gesture);

    if (const auto param = knobAt (p); param >= 0)
        return ui::valueHint (specFor (param, selectedSlot), valueOf (param, selectedSlot), gesture);

    ui::Hint h;
    if (const auto sc = sidechainButtonBounds(); ! sc.isEmpty() && sc.contains (p))
    {
        h.text = tr ("Sidechain input: the insert this effect listens to. Picking one routes it here at level 0; it keeps its own outputs  [LMB] choose");
        h.hasMenu = true;
        return h;
    }
    if (const auto* s = slots(); s != nullptr && selectedSlot >= 0 && selectedSlot < (int) s->size()
                                 && NativeEffectWindows::canOpen ((*s)[(std::size_t) selectedSlot].kind) && editorButtonBounds().contains (p))
    {
        h.text = tr ("Open the effect's window (its full controls, as in FL); clicking the slot's name does the same");
        return h;
    }
    if (track > 0 && armBounds().contains (p))
    {
        h.text = mixerinput::armHint (engine, track);
        h.hasMenu = true;
        return h;
    }
    if (track >= 0 && inputBoxBounds().contains (p))
    {
        h.text = track == 0 ? tr ("Audio input source: the microphone or interface input the Master plays (through its effects)  [LMB] choose")
                            : tr ("Audio input source: the microphone or interface input this insert plays and records  [LMB] choose");
        h.hasMenu = true;
        const auto& mt = engine.getSong().mixer[(std::size_t) track];
        if (mt.inputChannel >= 0)
            h.value = juce::String::fromUTF8 (inputSourceName (mt.inputChannel, mt.inputStereo, engine.getInputChannelNames()).c_str());
        return h;
    }
    if (fxSwitchBounds().contains (p) && slots() != nullptr && ! slots()->empty())
    {
        h.text = tr ("All effects of this insert on/off; right-click opens or closes their plugin windows");
        h.hasMenu = true;
        return h;
    }

    for (int i = 0; i < maxEffectSlots; ++i)
        if (slotBounds (i).contains (p))
        {
            const auto used = effectIn (i) >= 0;
            if (used && engine.isSlotLowLatencyBypassed (track, effectIn (i))) // round 190aq
            {
                const auto ms = 1000.0 * engine.lowLatencyBypassedLatency (track, effectIn (i)) / juce::jmax (1.0, engine.getTransport().sampleRate.load());
                h.text = tr ("Bypassed by \"Low latency while recording\": this effect adds {0} ms of latency and an armed or monitored insert plays through it. It comes back when that insert is disarmed",
                             { juce::String (ms, ms < 10.0 ? 1 : 0) });
                h.hasMenu = true;
                return h;
            }
            h.text = used ? (arrowBounds (i).contains (p) ? tr ("Effect slot options: replace, move, delete...")
                                                          : tr ("Effect slot: click shows or hides its window (and its knobs below), the LED bypasses, the wheel or a drag moves it up or down, right-click: options"))
                          : tr ("Empty slot: click to add an effect");
            h.hasMenu = used;
        }

    return h;
}

void EffectsPanel::mouseDrag (const juce::MouseEvent& e)
{
    if (dragSlot >= 0)
    {
        if (e.getDistanceFromDragStart() > 4)
        {
            const auto target = juce::jlimit (0, maxEffectSlots - 1, (e.y - slotsTop) / slotHeight); // (empty slots too, as FL)
            if (target != dropSlot)
            {
                dropSlot = target;
                setMouseCursor (juce::MouseCursor::DraggingHandCursor);
                repaint();
            }
        }
        return;
    }

    if (isEqPoint (dragParam))
    {
        setEqFromGraph (eqPointBase - dragParam, e.getPosition());
        return;
    }

    if (isEqParam (dragParam))
    {
        setValue (dragParam, -1, drag.update (e, specFor (dragParam, -1)));
        showTip (dragParam, -1, false);
        return;
    }

    if (dragParam == gateStepParam)
    {
        setGateStep (e.getPosition());
        return;
    }

    if (dragParam == bandLevelParam)
    {
        setBandLevel (e.getPosition());
        return;
    }

    if (dragParam == mixParam)
    {
        setValue (mixParam, selectedSlot, drag.update (e, specFor (mixParam, selectedSlot)));
        showTip (mixParam, selectedSlot, false);
        return;
    }

    if (dragParam < 0)
        return;

    setValue (dragParam, selectedSlot, drag.update (e, specFor (dragParam, selectedSlot)));
    showTip (dragParam, selectedSlot, false);
}

void EffectsPanel::showTip (int param, int slot, bool transient)
{
    if (isEqParam (param))
    {
        const auto band = (eqParamBase - param) / 3, what = (eqParamBase - param) % 3;
        const auto area = what == 0 ? eqFaderBounds (band) : eqKnobBounds (band, what);
        const auto spec = specFor (param, -1);
        ui::postHint (ui::valueHint (spec, valueOf (param, -1), ui::valueGestures()));
        if (transient)
            ui::flashValueTip (*this, area, spec.text (valueOf (param, -1)));
        else
            ui::showValueTip (*this, area, spec.text (valueOf (param, -1)));
        return;
    }

    const auto* s = slots();

    if (s == nullptr || slot < 0 || slot >= (int) s->size())
        return;

    const auto area = param == mixParam ? mixBounds (rowOf (slot)) : knobBounds (param).withHeight (juce::jlimit (30, 44, knobBounds (param).getHeight() - 30));
    const auto text = specFor (param, slot).text (valueOf (param, slot));
    ui::postHint (ui::valueHint (specFor (param, slot), valueOf (param, slot), ui::valueGestures()));

    if (transient)
        ui::flashValueTip (*this, area, text);
    else
        ui::showValueTip (*this, area, text);
}

void EffectsPanel::mouseMove (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    auto slot = -1; // (a row)
    auto param = -1;
    auto led = false;

    for (int i = 0; i < maxEffectSlots; ++i)
        if (slotBounds (i).contains (p))
            slot = i;

    if (const auto m = mixKnobAt (p); m >= 0)
    {
        slot = rowOf (m);
        param = mixParam;
    }
    else if (slot < 0)
        param = knobAt (p);
    else
        led = p.x < slotBounds (slot).getX() + 22;

    const auto input = track > 0 && armBounds().contains (p) ? 1 : track >= 0 && inputBoxBounds().contains (p) ? 2 : 0;
    const auto eq = eqControlAt (p);
    const auto output = outputBounds().contains (p);
    auto arrow = false;
    for (int i = 0; i < maxEffectSlots; ++i)
        if (arrowBounds (i).contains (p))
        {
            slot = i;
            arrow = true;
        }
    if (slot >= 0)
        led = ledBounds (slot).contains (p);

    if (const auto sc = sidechainButtonBounds(); sc.contains (p) != hoverSidechain) // round 177b
    {
        hoverSidechain = ! hoverSidechain;
        repaint (sc);
    }

    if (slot != hoverSlot || param != hoverParam || led != hoverLed || input != hoverInput || eq != hoverEq || output != hoverOutput || arrow != hoverArrow)
    {
        hoverEq = eq;
        hoverOutput = output;
        hoverArrow = arrow;
        hoverInput = input;
        hoverSlot = slot;
        hoverParam = param;
        hoverLed = led;
        repaint();
    }
}

void EffectsPanel::mouseExit (const juce::MouseEvent&)
{
    if (hoverSlot >= 0 || hoverParam != -1 || hoverInput != 0 || hoverEq != 0 || hoverOutput || hoverArrow)
    {
        hoverEq = 0;
        hoverOutput = hoverArrow = false;
        hoverInput = 0;
        hoverSlot = hoverParam = -1;
        hoverLed = false;
        repaint();
    }
}

void EffectsPanel::mouseUp (const juce::MouseEvent& e)
{
    // a click on the slot's name (not a drag, not the second click of a double-click) shows or hides its window (FL)
    if (dragSlot >= 0 && dropSlot < 0 && ! e.mouseWasDraggedSinceMouseDown() && e.getNumberOfClicks() == 1)
    {
        const auto slot = dragSlot;
        dragSlot = -1;
        toggleEditor (slot);
    }

    if (dragSlot >= 0 && dropSlot >= 0 && dropSlot != rowOf (dragSlot))
    {
        bool moved = false;
        engine.editSongStructural ([t = track, from = dragSlot, to = dropSlot, &moved] (Song& song) { moved = mixerops::moveEffectToRow (song, t, from, to); });
        if (moved)
            selectedSlot = effectIn (dropSlot);
    }

    if (dragSlot >= 0)
        setMouseCursor (juce::MouseCursor::NormalCursor);

    dragSlot = dropSlot = -1;
    repaint();
    dragParam = -1;
    drag.end();
    ui::hideValueTip();
    ui::clearPostedHint();
}

void EffectsPanel::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (const auto eqControl = eqControlAt (e.getPosition()); eqControl != 0)
    {
        if (isEqPoint (eqControl)) // the band back to 0 dB
            setValue (eqParamBase - (eqPointBase - eqControl) * 3, -1, specFor (eqParamBase, -1).defaultValue);
        else
            setValue (eqControl, -1, specFor (eqControl, -1).defaultValue);
        return;
    }

    for (int i = 0; slots() != nullptr && i < (int) slots()->size(); ++i)
        if (mixBounds (rowOf (i)).contains (e.getPosition()))
        {
            engine.editSong ([t = track, i] (Song& song) { song.mixer[(std::size_t) t].effects[(std::size_t) i].mix = 1.0f; });
            return;
        }
        else if (slotBounds (rowOf (i)).contains (e.getPosition()) && ! arrowBounds (rowOf (i)).contains (e.getPosition()) && ! ledBounds (rowOf (i)).contains (e.getPosition()))
        {
            openEditor (i); // (the first click opened it, round 170b): it stays open and comes to the front
            return;
        }

    const auto p = knobAt (e.getPosition());

    if (p < 0)
        return;

    const auto kind = (*slots())[(std::size_t) selectedSlot].kind;
    const auto value = effectInfo (kind).params[(std::size_t) p].defaultValue;
    engine.editSong ([t = track, slot = selectedSlot, p, value] (Song& song) {
        song.mixer[(std::size_t) t].effects[(std::size_t) slot].params[(std::size_t) p] = value;
    });
}

bool EffectsPanel::isInterestedInDragSource (const SourceDetails& details)
{
    return (BrowserComponent::isBrowserDrag (details) && BrowserComponent::draggedFile (details).hasFileExtension ("fst"))
           || BrowserComponent::isEffectDrag (details) || BrowserComponent::isGeneratorDrag (details);
}

void EffectsPanel::itemDropped (const SourceDetails& details)
{
    if (BrowserComponent::isGeneratorDrag (details))
    {
        if (auto c = BrowserComponent::draggedGenerator (details); c && onInstrumentDropped != nullptr
                                                                    && track >= 0 && track < (int) engine.getSong().mixer.size())
            onInstrumentDropped (track, std::move (*c));
        return;
    }
    if (BrowserComponent::isEffectDrag (details))
    {
        if (auto fx = BrowserComponent::draggedEffect (details))
            dropEffect (std::move (*fx), details.localPosition);
        return;
    }
    dropPreset (BrowserComponent::draggedFile (details), details.localPosition);
}

bool EffectsPanel::isInterestedInFileDrag (const juce::StringArray& files)
{
    return files.size() == 1 && juce::File (files[0]).hasFileExtension ("fst");
}

void EffectsPanel::filesDropped (const juce::StringArray& files, int x, int y)
{
    if (! files.isEmpty())
        dropPreset (juce::File (files[0]), { x, y });
}

bool EffectsPanel::dropPreset (const juce::File& file, juce::Point<int> position)
{
    juce::MemoryBlock data;

    if (slots() == nullptr || ! file.loadFileAsData (data))
        return false;

    const auto* bytes = static_cast<const std::uint8_t*> (data.getData());
    const auto fx = effectFromFlPreset (std::vector<std::uint8_t> (bytes, bytes + data.getSize()), engine.getSong().bpm, natives::flDataReader (flStudioAccess()));

    if (! fx)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, tr ("Preset"),
                                                tr ("LYNX SHARE can't build an effect from this preset."));
        return false;
    }

    return dropEffect (*fx, position);
}

bool EffectsPanel::dropEffect (EffectSlot fx, juce::Point<int> position)
{
    if (slots() == nullptr)
        return false;

    auto target = -1, emptyRow = -1;
    for (int row = 0; row < maxEffectSlots; ++row)
        if (slotBounds (row).contains (position))
        {
            target = effectIn (row);
            emptyRow = target < 0 ? row : -1;
        }

    if (target < 0 && (int) slots()->size() >= maxEffectSlots)
        return false;

    int added = -1;
    engine.editSong ([t = track, target, emptyRow, &added, fx = std::move (fx)] (Song& song) {
        auto& effects = song.mixer[(std::size_t) t].effects;
        if (target >= 0 && target < (int) effects.size())
            effects[(std::size_t) target] = fx;
        else if (emptyRow >= 0) // dropped on an empty slot: it lands there (FL)
            added = mixerops::insertEffectAtRow (song, t, fx, emptyRow);
        else
            effects.push_back (fx);
    });
    selectedSlot = target >= 0 ? target : added >= 0 ? added : (int) slots()->size() - 1;
    repaint();
    return true;
}

juce::Rectangle<int> EffectsPanel::fxSwitchBounds() const
{
    return { getWidth() - 58, 6, 48, panelHeader - 12 };
}

int EffectsPanel::slotAt (juce::Point<int> p) const
{
    const auto* s = slots();
    for (int i = 0; s != nullptr && i < (int) s->size(); ++i)
        if (const auto row = rowOf (i); slotBounds (row).contains (p) && ! mixBounds (row).contains (p) && ! ledBounds (row).contains (p) && ! arrowBounds (row).contains (p))
            return i;
    return -1;
}

void EffectsPanel::toggleAllWindows()
{
    // opens every plugin effect window of the insert, or closes them all when one is open
    const auto* s = slots();
    if (s == nullptr)
        return;

    bool anyOpen = false;
    for (int i = 0; i < (int) s->size(); ++i)
        if (auto* p = dynamic_cast<PluginEffect*> (engine.getEffect (track, i).get()))
            anyOpen = anyOpen || PluginWindows::isOpen (p->getInstance());

    for (int i = 0; i < (int) s->size(); ++i)
        if (auto* p = dynamic_cast<PluginEffect*> (engine.getEffect (track, i).get()))
        {
            if (anyOpen)
                PluginWindows::close (p->getInstance());
            else
                openPluginWindow (i);
        }
}

juce::Rectangle<int> EffectsPanel::pluginButtonBounds() const
{
    return { 20, slotsTop + maxEffectSlots * slotHeight + 30, getWidth() - 40, 30 };
}

void EffectsPanel::openEditor (int slot)
{
    const auto* s = slots();
    if (s == nullptr || slot < 0 || slot >= (int) s->size())
        return;
    const auto kind = (*s)[(std::size_t) slot].kind;
    if (kind == EffectKind::plugin)
        openPluginWindow (slot);
    else if (kind == EffectKind::parametricEq)
        ParametricEqWindows::open (engine, track, slot);
    else if (NativeEffectWindows::canOpen ((*s)[(std::size_t) slot]))
        NativeEffectWindows::open (engine, track, slot);
}

void EffectsPanel::toggleEditor (int slot)
{
    const auto* s = slots();
    if (s == nullptr || slot < 0 || slot >= (int) s->size())
        return;
    // FL (REA FUN_012bde10, the slot name button's click): the plugin's editor is shown, or hidden when it shows
    if ((*s)[(std::size_t) slot].kind == EffectKind::plugin)
    {
        if (auto* p = dynamic_cast<PluginEffect*> (engine.getEffect (track, slot).get()); p != nullptr && PluginWindows::isOpen (p->getInstance()))
        {
            PluginWindows::close (p->getInstance());
            return;
        }
        openPluginWindow (slot);
        return;
    }
    if (! ui::SlotGoneWatcher::closeWindow (engine, track, slot))
        openEditor (slot);
}

juce::Rectangle<int> EffectsPanel::editorButtonBounds() const
{
    // on the title row of the selected effect's knobs
    return { getWidth() - 96, (tuneSelected() ? tuneKeysBounds().getY() : knobBounds (0).getY()) - 29, 86, 20 };
}

void EffectsPanel::paintSidechainButton (juce::Graphics& g, const EffectSlot& slot)
{
    using namespace theme;
    const auto b = sidechainButtonBounds().toFloat();
    if (b.isEmpty())
        return;
    const auto source = pluginheader::sidechainSourceText (engine.getSong(), slot);
    const auto hover = b.contains (getMouseXYRelative().toFloat());
    theme::fillRoundRect (g, b, radius::md, hover ? colour::bg5 : colour::bg4);
    if (source.isNotEmpty()) // linked: the accent edge, as a lit route
    {
        g.setColour (colour::accent.withAlpha (0.8f));
        g.drawRoundedRectangle (b.reduced (0.5f), radius::md, 1.0f);
    }
    icons::draw (g, Icon::link, b.withWidth (22.0f).withSizeKeepingCentre (13.0f, 13.0f).translated (4.0f, 0.0f),
                 source.isNotEmpty() ? colour::accent : colour::textSecondary);
    const auto modulator = slot.kind == EffectKind::vocoder || slot.kind == EffectKind::vocodex;
    const auto text = (modulator ? tr ("Modulator") : tr ("SC")) + ": " + (source.isNotEmpty() ? source : tr ("None"));
    g.setColour (source.isNotEmpty() ? colour::textPrimary : colour::textSecondary);
    g.setFont (font (type::small));
    textcache::drawFittedText (g, text, b.withTrimmedLeft (24.0f).withTrimmedRight (14.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
    juce::Path tri; // ▾: it opens a list
    const auto a = juce::Point<float> (b.getRight() - 9.0f, b.getCentreY());
    tri.addTriangle (a.x - 3.5f, a.y - 2.0f, a.x + 3.5f, a.y - 2.0f, a.x, a.y + 2.5f);
    g.setColour (colour::textSecondary);
    g.fillPath (tri);
}

int EffectsPanel::titleRowY() const
{
    return (gateSelected() ? gateStepsBounds().getY() : vocoderSelected() ? bandLevelsBounds().getY()
            : tuneSelected() ? tuneKeysBounds().getY() : knobBounds (0).getY()) - 28;
}

juce::Rectangle<int> EffectsPanel::sidechainButtonBounds() const
{
    const auto* s = slots();
    if (s == nullptr || selectedSlot < 0 || selectedSlot >= (int) s->size() || track < 0)
        return {};
    const auto& slot = (*s)[(std::size_t) selectedSlot];
    if (slot.kind == EffectKind::facetEq || slot.kind == EffectKind::parametricEq || slot.kind == EffectKind::parallel
        || isSoundgoodizerApprox (slot)) // (Facet EQ: its own picker on its curve page)
        return {};
    if (! pluginheader::takesSidechain (engine, track, selectedSlot))
        return {};
    if (slot.kind == EffectKind::plugin) // under the plugin's name
    {
        const auto button = pluginButtonBounds();
        return { button.getX(), button.getBottom() + 38, button.getWidth(), 22 };
    }
    const auto editorButton = slot.kind != EffectKind::volumeGate && ! vocoderSelected() && NativeEffectWindows::canOpen (slot.kind);
    const auto right = editorButton ? editorButtonBounds().getX() - 6 : getWidth() - 10;
    const auto width = juce::jmin (130, juce::jmax (0, right - 90));
    return width < 60 ? juce::Rectangle<int>() : juce::Rectangle<int> (right - width, titleRowY(), width, 20);
}

void EffectsPanel::openPluginWindow (int slot)
{
    // Online Sessions P5-A1: an effect the session's host plays on this insert opens its parameter editor
    if (liveui::showHostedEditor ({ { live::Kind::insert, (std::uint64_t) juce::jmax (0, track), 0 }, slot }))
        return;
    auto effect = engine.getEffect (track, slot);

    if (auto* plugin = dynamic_cast<PluginEffect*> (effect.get()))
    {
        // round 190ao: crashed inside a call (not called again: its window would call into it), or its separate process
        // died: "Reload" starts it again without reopening the project
        const AudioEngine::HostedSlot where { -1, track, slot };
        auto reopen = [safe = juce::Component::SafePointer<EffectsPanel> (this), where] {
            if (safe == nullptr || safe->track != where.track)
                return;
            if (safe->engine.hostedPluginDown (where))
                PluginWindows::reload (safe->engine, { where });
            if (! safe->engine.hostedPluginDown (where))
                safe->openPluginWindow (where.slot);
        };
        if (engine.hostedPluginDown (where))
        {
            PluginWindows::promptReload (engine, where, [safe = juce::Component::SafePointer<EffectsPanel> (this), where] {
                if (safe != nullptr && safe->track == where.track)
                    safe->openPluginWindow (where.slot);
            });
            return;
        }
        const auto& s = engine.getSong().mixer[(std::size_t) track].effects[(std::size_t) slot];
        PluginWindows::WrapperContext context { &engine, [t = track, slot] (Song& song) -> PluginRef* {
                                                    if (t >= (int) song.mixer.size() || slot >= (int) song.mixer[(std::size_t) t].effects.size())
                                                        return nullptr;
                                                    return &song.mixer[(std::size_t) t].effects[(std::size_t) slot].plugin;
                                                }, false, track, slot, {}, {} };
        context.reload = reopen;
        context.down = [weak = std::weak_ptr<Effect> (effect)] {
            const auto held = weak.lock();
            auto* p = dynamic_cast<PluginEffect*> (held.get());
            return p != nullptr && p->hasCrashed();
        };
        PluginWindows::open (effect, plugin->getInstance(), quickpick::slotName (engine, s), std::move (context)); // (its Plugin database name, round 165c)
        return;
    }

    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Plugin"),
                                            tr ("This plugin could not be loaded (it is not installed or has not been scanned)."));
}

void EffectsPanel::showAddMenu (int row)
{
    juce::PopupMenu menu;

    // the effect lands in the clicked empty slot (FL's slot places, round 170b); returns its index, -1 = not added
    auto place = [this, row] (EffectSlot fx) {
        int at = -1;
        engine.editSong ([t = track, row, &fx, &at] (Song& song) {
            if (t < 0 || t >= (int) song.mixer.size() || (int) song.mixer[(std::size_t) t].effects.size() >= maxEffectSlots)
                return;
            at = mixerops::insertEffectAtRow (song, t, fx, row);
            if (at < 0)
            {
                song.mixer[(std::size_t) t].effects.push_back (std::move (fx));
                at = (int) song.mixer[(std::size_t) t].effects.size() - 1;
            }
            mixerops::linkAddedSidechain (song, t, at); // round 170h: the sole sidechain sender feeds a new compressor
        });
        if (at < 0)
            return at;
        selectedSlot = at;
        if ((*slots())[(std::size_t) at].kind != EffectKind::plugin) // (plugins: their own window, below)
            openEditor (at); // FL opens the effect's window when it is loaded (round 170h, fl-09)
        repaint();
        return at;
    };

    // FL's renamed plugins first (issue #11): "1_EQ" renamed in the Browser's Plugin database is added by typing 1
    auto addFx = [this, place] (EffectSlot fx) {
        const auto at = place (std::move (fx));
        if (at >= 0 && (*slots())[(std::size_t) at].kind == EffectKind::plugin)
            openPluginWindow (at);
    };
    auto picks = quickpick::renamedEffects (engine, addFx);
    quickpick::addPicks (menu, picks);
    // rounds 166f / 170b (Busy Works: "random order"): every effect under its category heading, A-Z, as FL's slot menu
    // (LYNX SHARE first, then FL's Plugin database categories, the user's own folders among them)
    quickpick::addFolderGroups (menu, engine, std::function<void (EffectSlot)> (addFx));

    // LYNX equivalents of well-known third-party effects, with the same default preset a substitute gets
    juce::PopupMenu equivalents;
    for (const auto& entry : pluginSubstitutes())
        if (! entry.instrument)
            equivalents.addItem (juce::String::fromUTF8 (entry.product.c_str()) + juce::String::fromUTF8 (" \xe2\x86\x92 ") + juce::String::fromUTF8 (effectInfo (entry.effect).name),
                                 [place, entry] { place (makeSubstituteSlot (entry)); });
    if (equivalents.getNumItems() > 0)
        menu.addSubMenu (tr ("LYNX SHARE equivalents"), equivalents);

    juce::PopupMenu plugins;
    addPluginItems (plugins, engine, false, [this, place] (PluginRef ref) {
        EffectSlot slot;
        slot.kind = EffectKind::plugin;
        slot.plugin = ref;
        if (const auto at = place (std::move (slot)); at >= 0)
            openPluginWindow (at);
    }, this);
    menu.addSeparator();
    menu.addSubMenu (tr ("Plugin (VST/VST3/CLAP)"), plugins);
    menu.addSeparator();
    menu.addItem (tr ("Audio editor (record this insert)..."), [this] { openAudioEditorForInsert (engine, track); });
    quickpick::addFolderViewItems (menu, engine); // FL: Categories / Simple / Tree at the bottom

    quickpick::showMenu (menu, juce::PopupMenu::Options().withTargetComponent (menuOwner()).withMousePosition(), std::move (picks));
}

void EffectsPanel::showSlotMenu (int slot)
{
    // the same categories as the empty slot's menu (rounds 166f / 170b)
    juce::PopupMenu replace;
    quickpick::addFolderGroups (replace, engine, std::function<void (EffectSlot)> ([this, slot] (EffectSlot fx) {
        engine.editSong ([t = track, slot, fx] (Song& song) {
            if (t < (int) song.mixer.size() && slot < (int) song.mixer[(std::size_t) t].effects.size())
                song.mixer[(std::size_t) t].effects[(std::size_t) slot] = fx;
        });
        if (slot < (int) slots()->size() && (*slots())[(std::size_t) slot].kind == EffectKind::plugin)
            openPluginWindow (slot);
    }));
    quickpick::addFolderViewItems (replace, engine);

    juce::PopupMenu menu;
    if (extendSlotMenu != nullptr) // (round 183a: the Device View's card adds Fold / Unfold first; the Mixer adds nothing)
        extendSlotMenu (menu, slot);
    menu.addSubMenu (tr ("Replace with"), replace);
    // round 190ce: hot-swap from the Mixer too (the Device View's card has its own item and button)
    if (extendSlotMenu == nullptr && onHotSwapSlot != nullptr)
        menu.addItem (tr ("Hot-swap in the Browser"), [t = track, slot] {
            if (onHotSwapSlot != nullptr)
                onHotSwapSlot (t, slot);
        });
    menu.addItem (tr ("Audio editor (record this insert)..."), [this] { openAudioEditorForInsert (engine, track); });

    // FL presets (Plugin presets\Effects\<plugin>) that map to this effect
    {
        juce::PopupMenu presets;
        const auto roots = findFlStudioRoots();
        const auto base = juce::File (juce::String::fromUTF8 (roots.data.c_str())).getChildFile ("Patches/Plugin presets/Effects");

        for (const auto& plugin : flPluginsForEffect ((*slots())[(std::size_t) slot].kind))
        {
            juce::PopupMenu sub;
            auto files = base.getChildFile (juce::String::fromUTF8 (plugin.c_str())).findChildFiles (juce::File::findFiles, true, "*.fst");
            std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName().compareNatural (b.getFileName()) < 0; });

            for (const auto& f : files)
                sub.addItem (f.getFileNameWithoutExtension(), [this, slot, f] {
                    juce::MemoryBlock data;
                    if (! f.loadFileAsData (data))
                        return;
                    const auto* bytes = static_cast<const std::uint8_t*> (data.getData());
                    if (auto fx = effectFromFlPreset (std::vector<std::uint8_t> (bytes, bytes + data.getSize()), engine.getSong().bpm, natives::flDataReader (flStudioAccess())))
                        engine.editSong ([t = track, slot, fx = *fx] (Song& song) {
                            auto& target = song.mixer[(std::size_t) t].effects[(std::size_t) slot];
                            const auto enabled = target.enabled;
                            const auto mix = target.mix;
                            target = fx;
                            target.enabled = enabled;
                            target.mix = mix;
                        });
                });

            if (sub.getNumItems() > 0)
                presets.addSubMenu (juce::String::fromUTF8 (plugin.c_str()), sub);
        }

        if (presets.getNumItems() > 0)
            menu.addSubMenu (tr ("FL presets"), presets);
    }

    // side inputs: another track's output feeds the compressor's detector, or the vocoder's modulator/carrier. Round
    // 177b: every effect with a side input (plugins with a sidechain bus too) gets the picker, which makes the route
    const auto kind = (*slots())[(std::size_t) slot].kind;
    auto sideMenu = [&] (const juce::String& title, bool second) {
        juce::PopupMenu side;
        pluginheader::addSidechainPicker (side, engine, track, slot, second);
        menu.addSubMenu (title, side);
    };
    if (kind == EffectKind::vocoder || kind == EffectKind::vocodex)
    {
        sideMenu (tr ("Modulator from"), false);
        sideMenu (tr ("Carrier from"), true);
    }
    else if (pluginheader::takesSidechain (engine, track, slot))
        sideMenu (tr ("Sidechain from"), false);
    if (kind == EffectKind::newtone || kind == EffectKind::newtime)
    {
        // the sample the plugin plays in slave mode (REA_rodada73): FL's edits are kept in the state but not reproduced
        const auto& fx = (*slots())[(std::size_t) slot];
        const auto current = juce::File (juce::String::fromUTF8 (fx.samplePath.c_str()));
        juce::PopupMenu source;
        source.addItem (fx.samplePath.empty() ? tr ("No sample") : current.getFileName(), false, false, [] {});
        source.addItem (tr ("Choose a sample..."), [this, slot, current] {
            sampleChooser = lynx::ui::makeFileChooser (tr ("Source sample"), current.existsAsFile() ? current : juce::File(),
                                                                 "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
            lynx::ui::launchChooser (sampleChooser, juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                        [this, slot] (const juce::FileChooser& fc) {
                                            const auto file = fc.getResult();
                                            if (file == juce::File())
                                                return;
                                            engine.editSong ([t = track, slot, path = file.getFullPathName().toStdString()] (Song& s) {
                                                if (t < (int) s.mixer.size() && slot < (int) s.mixer[(std::size_t) t].effects.size())
                                                    s.mixer[(std::size_t) t].effects[(std::size_t) slot].samplePath = path;
                                            });
                                        });
        });
        source.addItem (tr ("Remove the sample"), ! fx.samplePath.empty(), false, [this, slot] {
            engine.editSong ([t = track, slot] (Song& s) {
                if (t < (int) s.mixer.size() && slot < (int) s.mixer[(std::size_t) t].effects.size())
                    s.mixer[(std::size_t) t].effects[(std::size_t) slot].samplePath.clear();
            });
        });
        menu.addSubMenu (tr ("Source sample"), source);
    }
    {
        // A/B compare: keep a copy of the settings, then swap between it and the current ones
        const auto key = std::make_pair (track, slot);
        const auto hasSpare = spare.count (key) > 0 && spare[key].kind == (*slots())[(std::size_t) slot].kind;
        juce::PopupMenu ab;
        ab.addItem (tr ("Store the current settings as B"), [this, key, slot] {
            spare[key] = (*slots())[(std::size_t) slot];
        });
        ab.addItem (tr ("Swap with B"), hasSpare, false, [this, key, slot] {
            const auto stored = spare[key];
            spare[key] = (*slots())[(std::size_t) slot];
            engine.editSong ([t = track, slot, stored] (Song& song) {
                auto& target = song.mixer[(std::size_t) t].effects[(std::size_t) slot];
                const auto enabled = target.enabled;
                target = stored;
                target.enabled = enabled;
            });
        });
        ab.addItem (tr ("Forget B"), hasSpare, false, [key] { spare.erase (key); });
        menu.addSubMenu (hasSpare ? tr ("A/B compare (B stored)") : tr ("A/B compare"), ab);
    }
    if ((*slots())[(std::size_t) slot].isSubstitute()) // A/B of every substitute of the project at once (round 113)
        menu.addItem (tr ("Bypass all LYNX SHARE substitutes of this project"), true, engine.getSong().substitutesBypassed, [this] {
            engine.editSong ([] (Song& song) { song.substitutesBypassed = ! song.substitutesBypassed; });
            repaint();
        });
    // FL's slot menu "CPU" section (REA round 139): Allow threaded processing (plugins), Smart disable
    menu.addSectionHeader (tr ("CPU"));
    if ((*slots())[(std::size_t) slot].kind == EffectKind::plugin)
        menu.addItem (tr ("Allow threaded processing"), true, (*slots())[(std::size_t) slot].plugin.threaded, [this, slot] {
            engine.editSong ([t = track, slot] (Song& song) {
                if (t < (int) song.mixer.size() && slot < (int) song.mixer[(std::size_t) t].effects.size())
                    song.mixer[(std::size_t) t].effects[(std::size_t) slot].plugin.threaded = ! song.mixer[(std::size_t) t].effects[(std::size_t) slot].plugin.threaded;
            });
        });
    // FL's per-plugin "Smart disable": skipped while its input and output are silent
    menu.addItem (tr ("Smart disable"), true, (*slots())[(std::size_t) slot].smartDisable, [this, slot] {
        engine.editSong ([t = track, slot] (Song& song) {
            if (t < (int) song.mixer.size() && slot < (int) song.mixer[(std::size_t) t].effects.size())
                song.mixer[(std::size_t) t].effects[(std::size_t) slot].smartDisable = ! song.mixer[(std::size_t) t].effects[(std::size_t) slot].smartDisable;
        });
    });
    menu.addSeparator();
    // FL's Move up / Move down (REA FUN_01233de0 / FUN_01235190): one slot, into an empty one too, wrapping around
    auto move = [this, slot] (int delta) {
        int now = -1;
        engine.editSongStructural ([t = track, slot, delta, &now] (Song& song) { now = mixerops::moveEffectBy (song, t, slot, delta); });
        if (now >= 0)
            selectedSlot = now;
        repaint();
    };
    menu.addItem (tr ("Move up"), [move] { move (-1); });
    menu.addItem (tr ("Move down"), [move] { move (1); });
    if ((*slots())[(std::size_t) slot].kind == EffectKind::plugin)
    {
        // round 190ao: only while the plugin is down (it crashed, or its separate process died)
        if (engine.hostedPluginDown ({ -1, track, slot }))
            menu.addItem (tr ("Reload crashed plugin"), [this, t = track, slot] { PluginWindows::reload (engine, { { -1, t, slot } }); });
        auto* live = dynamic_cast<PluginEffect*> (engine.getEffect (track, slot).get());
        menu.addSubMenu (tr ("Processing"), pluginProcessingMenu (engine, (*slots())[(std::size_t) slot].plugin, live != nullptr ? &live->getInstance() : nullptr,
                                                                [t = track, slot] (Song& song) -> PluginRef* {
                                                                    if (t >= (int) song.mixer.size() || slot >= (int) song.mixer[(std::size_t) t].effects.size())
                                                                        return nullptr;
                                                                    return &song.mixer[(std::size_t) t].effects[(std::size_t) slot].plugin;
                                                                }));
    }
    if ((*slots())[(std::size_t) slot].kind == EffectKind::plugin)
    {
        if (auto* p = dynamic_cast<PluginEffect*> (engine.getEffect (track, slot).get()))
            addPluginParameterItems (menu, engine, p->getInstance(), { AutomationTargetKind::effectParam, track, slot, 0 });
    }
    if ((*slots())[(std::size_t) slot].kind == EffectKind::patcher) // its published parameters (round 171b)
        addPatcherParameterItems (menu, engine, (*slots())[(std::size_t) slot].patcher, { AutomationTargetKind::effectParam, track, slot, 0 });

    // FL's Patcherize / Patcherize all (round 165m): the effect (or the whole chain) into a new Patcher, same sound
    menu.addSeparator();
    menu.addItem (tr ("Patcherize"), patcherize::canPatcherizeSlot (engine.getSong(), track, slot), false, [this, slot] {
        engine.capturePluginStates(); // (the plugins' current settings go with them)
        int placed = -1;
        engine.editSong ([t = track, slot, &placed] (Song& song) { placed = patcherize::patcherizeSlots (song, t, slot, false); });
        if (placed >= 0)
            selectedSlot = placed;
        repaint();
    });
    menu.addItem (tr ("Patcherize all"), patcherize::slotsForAll (engine.getSong(), track).size() > 1, false, [this, slot] {
        engine.capturePluginStates();
        int placed = -1;
        engine.editSong ([t = track, slot, &placed] (Song& song) { placed = patcherize::patcherizeSlots (song, t, slot, true); });
        if (placed >= 0)
            selectedSlot = placed;
        repaint();
    });
    // round 183c: Live's Group (Ctrl+G): the effect (or the whole chain) into a new Audio Effect Rack, same sound; the
    // automation of the moved effects' parameters drives the rack's macros
    menu.addItem (tr ("Group into Audio Effect Rack"), patcherize::canPatcherizeSlot (engine.getSong(), track, slot), false, [this, slot] { groupIntoRack (slot, false); });
    menu.addItem (tr ("Group all into Audio Effect Rack"), patcherize::slotsForAll (engine.getSong(), track).size() > 1, false, [this, slot] { groupIntoRack (slot, true); });
    // Live's Ungroup: a one-chain Audio Effect Rack's devices back into the slots
    if (const auto* list = slots(); list != nullptr && slot < (int) list->size() && rack::isRack ((*list)[(std::size_t) slot]))
    {
        const auto* r = rack::rackOf ((*list)[(std::size_t) slot].patcher);
        const auto canUngroup = r != nullptr && r->kind == RackDoc::Kind::audioEffect && r->chains.size() == 1;
        menu.addItem (tr ("Ungroup"), canUngroup, false, [this, slot] {
            engine.capturePluginStates();
            int placed = -1;
            engine.editSong ([t = track, slot, &placed] (Song& song) { placed = rack::ungroupSlot (song, t, slot); });
            if (placed >= 0)
                selectedSlot = placed;
            repaint();
        });
    }
    menu.addSeparator();
    menu.addItem (tr ("Delete") + "\tDel", [this, slot] { deleteSlot (slot); });

    if (slotMenuForSnapshot != nullptr) // (the shot routes: the menu itself, nothing opens)
    {
        *slotMenuForSnapshot = menu;
        return;
    }
    // FL: with the slot's menu open, Delete removes the plugin (issue #11)
    quickpick::showMenu (menu, juce::PopupMenu::Options().withTargetComponent (menuOwner()).withMousePosition(), {},
                         [safe = juce::Component::SafePointer<EffectsPanel> (this), slot] (const juce::KeyPress& key) {
                             if (! key.isKeyCode (juce::KeyPress::deleteKey) || safe == nullptr)
                                 return false;
                             juce::MessageManager::callAsync ([safe, slot] {
                                 if (safe != nullptr)
                                     safe->deleteSlot (slot);
                             });
                             return true;
                         });
}

void EffectsPanel::deleteSlot (int slot)
{
    const auto* s = slots();
    if (s == nullptr || slot < 0 || slot >= (int) s->size())
        return;
    engine.editSongStructural ([t = track, slot] (Song& song) { mixerops::removeEffectKeepRows (song, t, slot); }); // (FL: the slot stays empty)
    selectedSlot = juce::jmin (slot, (int) slots()->size() - 1);
    repaint();
}

void EffectsPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    const auto* s = slots();

    if (s == nullptr)
        track = 0;
    else if (selectedSlot >= (int) s->size())
        selectedSlot = (int) s->size() - 1;

    refit(); // the clean mixer (round 163) changes the height the panel needs
    repaint();
}

// ---------------------------------------------------------------------------

namespace mixerinput
{
std::function<void()> openAudioSettings;

namespace
{
juce::String issueText (const AudioEngine::RecordingNotice& notice)
{
    using I = AudioEngine::InputIssue;
    switch (notice.issue)
    {
        case I::noDevice: return tr ("No audio device is open. Choose one in Settings > Audio.");
        case I::noInputDevice:
            return tr ("No audio input device was found. Connect a microphone or an audio interface, then choose it as the input device in Settings > Audio.");
        case I::noInputOpen: return tr ("No audio input is open, so nothing can be recorded. Choose an input device in Settings > Audio.");
        case I::armedWithoutInput:
            return tr ("Please select an audio input first: the armed insert has none. Click the input box above the insert's effect slots in the Mixer.");
        case I::insertWithoutInput: return tr ("Insert {0} is armed but has no audio input, so it records nothing.", { juce::String (notice.track) });
        case I::failed: return tr ("The audio input could not be used: {0}", { notice.detail });
        case I::lowDisk:
            return tr ("The recordings drive is almost full: about {0} minute(s) of recording left. Free some space or choose another project folder.",
                       { notice.detail });
        case I::recordSafe:
            return tr ("Insert {0} is record safe, so it can't be armed. Click its input box and turn \"Record safe\" off first.",
                       { juce::String (notice.track) });
        case I::framesLost:
            return tr ("The disk could not keep up while recording: {0} sample(s) became silence (the timing of the take was kept). "
                       "Record to a faster drive or close other programs that use the disk.", { notice.detail });
        case I::none: break;
    }
    return {};
}

bool aboutTheDevice (AudioEngine::InputIssue issue)
{
    using I = AudioEngine::InputIssue;
    return issue == I::noDevice || issue == I::noInputDevice || issue == I::noInputOpen || issue == I::failed;
}
} // namespace

void explain (const AudioEngine::RecordingNotice& notice)
{
    if (notice.issue == AudioEngine::InputIssue::none)
        return;

    using I = AudioEngine::InputIssue;
    const auto aboutRecording = notice.issue == I::lowDisk || notice.issue == I::recordSafe || notice.issue == I::framesLost;
    auto options = juce::MessageBoxOptions()
                       .withIconType (juce::MessageBoxIconType::WarningIcon)
                       .withTitle (aboutRecording ? tr ("Recording") : tr ("Audio input needed"))
                       .withMessage (issueText (notice));

    if (aboutTheDevice (notice.issue) && openAudioSettings)
    {
        options = options.withButton (tr ("Audio settings...")).withButton (tr ("Close"));
        juce::AlertWindow::showAsync (options, [] (int result) {
            if (result == 1 && openAudioSettings)
                openAudioSettings();
        });
    }
    else
        juce::AlertWindow::showAsync (options.withButton (tr ("OK")), nullptr);
}

// Round 157 ([DATA] the 0.1.15 projects of a singer who found that "the effects do nothing" on her voice): her
// microphone was the input of both Insert 1 (with the effects) and the Master. The Master plays its input straight
// away, without any insert's effects, so the dry voice covered the processed one. When an input is chosen and the
// Master plays the same device channel as an insert, LYNX says so and offers to take it off the Master.
void warnDoubleMonitoring (AudioEngine& engine, int track)
{
    const auto& mixer = engine.getSong().mixer;
    if (mixer.empty() || track < 0 || track >= (int) mixer.size())
        return;
    auto channels = [] (const MixerTrack& t) {
        std::pair<int, int> r { t.inputChannel, t.inputChannel + (t.inputStereo ? 1 : 0) };
        return r;
    };
    auto overlaps = [&] (const MixerTrack& a, const MixerTrack& b) {
        if (a.inputChannel < 0 || b.inputChannel < 0)
            return false;
        const auto [a0, a1] = channels (a);
        const auto [b0, b1] = channels (b);
        return a0 <= b1 && b0 <= a1;
    };
    const auto& master = mixer[0];
    if (master.inputChannel < 0 || master.monitor == InputMonitor::off)
        return;
    int insert = -1;
    for (int t = 1; t < (int) mixer.size() && insert < 0; ++t)
        if ((track == 0 || t == track) && overlaps (master, mixer[(std::size_t) t]))
            insert = t;
    if (insert < 0)
        return;
    const auto name = juce::String::fromUTF8 (mixer[(std::size_t) insert].name.c_str());
    auto options = juce::MessageBoxOptions()
                       .withIconType (juce::MessageBoxIconType::WarningIcon)
                       .withTitle (tr ("The same input plays twice"))
                       .withMessage (tr ("This audio input is the input of the Master and of {0}. The Master plays it straight away, without "
                                         "{0}'s effects, so you hear the dry sound on top of the processed one (the effects seem to do nothing).\n\n"
                                         "To hear it through {0}'s effects, take it off the Master.", { name.isNotEmpty() ? name : tr ("Insert {0}", { juce::String (insert) }) }))
                       .withButton (tr ("Take it off the Master"))
                       .withButton (tr ("Keep both"));
    juce::AlertWindow::showAsync (options, [&engine] (int result) {
        if (result == 1)
            engine.setInsertInput (0, -1, true);
    });
}

// FL's question when an external input is chosen on an insert that records more than it (core/LiveInput.h
// asksExternalInputOnly, issue #152): Yes = "External input only", so the take holds the input alone.
void askExternalInputOnly (AudioEngine& engine, int track)
{
    const auto& mixer = engine.getSong().mixer;
    if (track < 0 || track >= (int) mixer.size())
        return;
    const auto& mt = mixer[(std::size_t) track];
    if (! asksExternalInputOnly (track, mt.inputChannel, mt.recordSource))
        return;
    auto options = juce::MessageBoxOptions()
                       .withIconType (juce::MessageBoxIconType::QuestionIcon)
                       .withTitle (tr ("External input"))
                       .withMessage (tr ("You've selected an external input. Do you want to switch to \"External input only\" to avoid recording "
                                         "internal audio on this track?"))
                       .withButton (tr ("Yes"))
                       .withButton (tr ("No"));
    juce::AlertWindow::showAsync (options, [&engine, track] (int result) {
        if (result == 1)
            engine.setInsertRecordSource (track, RecordSource::externalInput);
    });
}

void showMenu (AudioEngine& engine, int track, juce::Component* target, std::function<void()> chosen)
{
    if (track < 0 || track >= (int) engine.getSong().mixer.size())
        return;

    // FL: "The input of a mixer track can't be changed while recording." (a take keeps the channels it started with;
    // issue #152)
    if (engine.isRecordingAudioTakes())
    {
        juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                          .withIconType (juce::MessageBoxIconType::WarningIcon)
                                          .withTitle (tr ("Warning"))
                                          .withMessage (tr ("The input of a mixer track can't be changed while recording."))
                                          .withButton (tr ("OK")),
                                      nullptr);
        return;
    }

    // the device input opens on demand (only then does Windows show the microphone as in use)
    if (! engine.hasAudioInput())
        if (const auto result = engine.openAudioInput(); result.issue != AudioEngine::InputIssue::none)
        {
            explain (result);
            return;
        }

    const auto& mt = engine.getSong().mixer[(std::size_t) track];
    const auto names = engine.getInputChannelNames();
    juce::PopupMenu menu;
    menu.addSectionHeader (tr ("Audio input source"));
    menu.addItem (tr ("(none)"), true, mt.inputChannel < 0, [&engine, track] { engine.setInsertInput (track, -1, true); });

    const auto sources = inputSources (names);
    if (sources.empty())
        menu.addItem (tr ("The audio device has no open input channels"), false, false, nullptr);

    for (const auto& source : sources)
    {
        if (! source.stereo && source.channel == 0 && names.size() > 1)
            menu.addSeparator(); // pairs, then single channels
        const auto ticked = mt.inputChannel == source.channel && mt.inputStereo == source.stereo;
        menu.addItem (juce::String::fromUTF8 (source.name.c_str()) + (source.stereo ? "" : "  " + tr ("(mono)")), true, ticked,
                      [&engine, track, source, chosen] {
                          engine.setInsertInput (track, source.channel, source.stereo);
                          warnDoubleMonitoring (engine, track);
                          askExternalInputOnly (engine, track);
                          if (chosen)
                              chosen();
                      });
    }

    if (track > 0) // FL's "Record" positions (round 168x, issue #41; FL's AudioRecorderRoutingMenu, in its order)
    {
        menu.addSectionHeader (tr ("Record"));
        const std::pair<RecordSource, const char*> points[] = { { RecordSource::externalInput, TR_NOOP ("External input only") },
                                                               { RecordSource::externalAndMixer, TR_NOOP ("External and mixer input") },
                                                               { RecordSource::postEffects, TR_NOOP ("Post effects") },
                                                               { RecordSource::postEq, TR_NOOP ("Post EQ") },
                                                               { RecordSource::postLevel, TR_NOOP ("Post level and panning") },
                                                               { RecordSource::postTrack, TR_NOOP ("Post track") } };
        for (const auto& [point, label] : points)
            menu.addItem (tr (label), true, mt.recordSource == point, [&engine, track, point = point] { engine.setInsertRecordSource (track, point); });
        // LYNX extra: the dry device input as a second take beside a processed one (DI + amp, for re-amping)
        menu.addItem (tr ("Also record the dry input as a second take (LYNX)"), mt.recordSource != RecordSource::externalInput,
                      mt.recordDryToo && mt.recordSource != RecordSource::externalInput,
                      [&engine, track, on = mt.recordDryToo] { engine.setInsertRecordDryToo (track, ! on); });
    }

    menu.addSectionHeader (tr ("Monitor external input"));
    const std::pair<InputMonitor, const char*> modes[] = { { InputMonitor::off, TR_NOOP ("Off") },
                                                          { InputMonitor::whenArmed, TR_NOOP ("When armed") },
                                                          { InputMonitor::automatic, TR_NOOP ("Auto (when armed, not while playing back)") },
                                                          { InputMonitor::on, TR_NOOP ("On") } };
    for (const auto& [mode, label] : modes)
    {
        // the Master has no record arm: its input plays unless monitoring is off
        if (track == 0 && (mode == InputMonitor::whenArmed || mode == InputMonitor::automatic))
            continue;
        const auto ticked = track == 0 ? (mode == InputMonitor::off) == (mt.monitor == InputMonitor::off) : mt.monitor == mode;
        menu.addItem (tr (label), true, ticked, [&engine, track, mode = mode] { engine.setInsertMonitor (track, mode); });
    }

    if (track > 0) // record safe (round 163): the insert can't be armed by accident
    {
        menu.addSeparator();
        menu.addItem (tr ("Record safe (can't be armed)"), true, mt.recordSafe,
                      [&engine, track, safe = mt.recordSafe] { engine.setInsertRecordSafe (track, ! safe); });
    }

    menu.addSeparator();
    menu.addItem (tr ("Audio settings (input device)..."), openAudioSettings != nullptr, false, [] {
        if (openAudioSettings)
            openAudioSettings();
    });

    auto options = juce::PopupMenu::Options();
    if (target != nullptr)
        options = options.withTargetComponent (target).withMousePosition();
    menu.showMenuAsync (options);
}

juce::String armHint (const AudioEngine& engine, int track)
{
    auto text = tr ("Record arm: Rec + Play records this insert into a Playlist clip  [RMB] record to a file");
    if (track <= 0 || track >= (int) engine.getSong().mixer.size())
        return text;
    // FL's hint names the position: "Recording: Post EQ" (round 168x; changed in the input box's menu)
    static const char* const points[] { TR_NOOP ("External input only"), TR_NOOP ("External and mixer input"), TR_NOOP ("Post effects"),
                                        TR_NOOP ("Post EQ"), TR_NOOP ("Post level and panning"), TR_NOOP ("Post track") };
    const auto& mt = engine.getSong().mixer[(std::size_t) track];
    text = tr ("Recording: {0}", { tr (points[juce::jlimit (0, 5, (int) mt.recordSource)]) })
           + (mt.recordDryToo && mt.recordSource != RecordSource::externalInput ? " + " + tr ("dry input") : juce::String()) + "  " + text;
    if (mt.recordTo.empty())
        return text;
    const auto file = juce::File (juce::String::fromUTF8 (engine.getSong().mixer[(std::size_t) track].recordTo.c_str()));
    return tr ("Record to: {0}", { file.getFileName() }) + "  " + text;
}

void chooseRecordTo (AudioEngine& engine, int track)
{
    if (track <= 0 || track >= (int) engine.getSong().mixer.size())
        return;
    if (engine.getSong().mixer[(std::size_t) track].recordSafe)
    {
        AudioEngine::RecordingNotice notice;
        notice.issue = AudioEngine::InputIssue::recordSafe;
        notice.track = track;
        explain (notice);
        return;
    }
    const auto insert = juce::String::fromUTF8 (mixerops::insertDisplayName (engine.getSong(), track).c_str());
    const auto& current = engine.getSong().mixer[(std::size_t) track].recordTo;
    juce::File initial;
    if (! current.empty() && juce::File::isAbsolutePath (juce::String::fromUTF8 (current.c_str())))
        initial = juce::File (juce::String::fromUTF8 (current.c_str()));
    else
    {
        // FL's default: Audio\Recorded, "<project title>_YYYY-MM-DD HH-MM-SS_<insert name>"
        auto folder = engine.getRecordingFolder();
        folder.createDirectory();
        const auto now = juce::Time::getCurrentTime();
        const auto name = juce::File::createLegalFileName (juce::String::fromUTF8 (
            safety::recordingName (engine.getRecordingStem().toStdString(), now.getYear(), now.getMonth() + 1, now.getDayOfMonth(), now.getHours(),
                                   now.getMinutes(), now.getSeconds(), insert.toStdString()).c_str()));
        initial = folder.getChildFile (name + ".wav");
    }
    static std::unique_ptr<juce::FileChooser> chooser;
    chooser = lynx::ui::makeFileChooser (tr ("Record {0} to", { insert }), initial, "*.wav");
    lynx::ui::launchChooser (chooser, juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [&engine, track] (const juce::FileChooser& fc) {
                              auto file = fc.getResult();
                              if (file == juce::File() || track >= (int) engine.getSong().mixer.size())
                                  return;
                              if (! file.hasFileExtension (".wav"))
                                  file = file.withFileExtension (".wav");
                              engine.editSong ([track, path = file.getFullPathName().toStdString()] (Song& s) {
                                  if (track < (int) s.mixer.size())
                                      s.mixer[(std::size_t) track].recordTo = path;
                              });
                              if (! engine.isInsertArmed (track)) // FL arms it (and the insert gets selected)
                                  toggleArm (engine, track, nullptr);
                          });
}

void toggleArm (AudioEngine& engine, int track, juce::Component* /*target*/)
{
    if (track < 0 || track >= (int) engine.getSong().mixer.size())
        return;

    // FL 2026: arms at once, also without an input source (the recording start reports a missing input)
    switch (armClick (track, engine.isInsertArmed (track)))
    {
        case ArmClick::none: return;
        case ArmClick::disarm: engine.setInsertArmed (track, false); return;
        case ArmClick::arm:
            engine.setInsertArmed (track, true);
            if (const auto notice = engine.takeRecordingNotice(); notice.issue != AudioEngine::InputIssue::none)
                explain (notice);
            return;
    }
}
} // namespace mixerinput

// ---------------------------------------------------------------------------

MixerLayoutBox::MixerLayoutBox (MixerStrips& s)
    : strips (s)
{
    setTooltip (tr ("Mixer layout"));
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

juce::PopupMenu MixerLayoutBox::menu (MixerStrips& strips)
{
    juce::PopupMenu m;
    for (int i = 0; i < mixerlayout::layoutCount; ++i)
    {
        const auto l = (mixerlayout::Layout) i;
        m.addItem (tr (juce::String (std::string (mixerlayout::nameOf (l)))), true, MixerStrips::layout == l,
                   [&strips, l] { strips.setLayout (l); });
        if (mixerlayout::separatorAfter (l))
            m.addSeparator();
    }
    return m;
}

void MixerLayoutBox::paint (juce::Graphics& g)
{
    using namespace theme;
    const auto hot = isMouseOverOrDragging();
    auto box = getLocalBounds().toFloat().reduced (1.0f, 2.0f);
    theme::fillRoundRect (g, box, radius::sm, hot ? colour::bg4 : colour::bg2);
    g.setColour (hot ? colour::borderStrong : colour::border);
    g.drawRoundedRectangle (box.reduced (0.5f), radius::sm, 1.0f);
    // round 170e: the strips icon names what the box is (it showed only "Wide")
    icons::draw (g, Icon::mixer, box.withWidth (24.0f).withSizeKeepingCentre (14.0f, 14.0f), hot ? colour::textPrimary : colour::textSecondary);
    g.setColour (colour::accent);
    g.setFont (font (type::small, true));
    g.drawFittedText (tr (juce::String (std::string (mixerlayout::nameOf (MixerStrips::layout)))), getLocalBounds().reduced (8, 0).withTrimmedLeft (18).withTrimmedRight (14),
                      juce::Justification::centredLeft, 1, 0.8f);
    icons::draw (g, Icon::chevronDown, box.removeFromRight (18.0f).withSizeKeepingCentre (12.0f, 12.0f), hot ? colour::textPrimary : colour::textSecondary);
}

void MixerLayoutBox::mouseDown (const juce::MouseEvent&)
{
    menu (strips).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
}

MixerComponent::MixerComponent (AudioEngine& e)
    : engine (e),
      strips (e),
      effects (e)
{
    viewport.setViewedComponent (&strips, false);
    viewport.setScrollBarsShown (false, true);
    addAndMakeVisible (viewport);
    effectsView.setViewedComponent (&effects, false);
    effectsView.setScrollBarsShown (true, false);
    effectsView.setScrollBarThickness (10);
    addAndMakeVisible (effectsView);
    strips.onSelectionChanged = [this] (int track) { effects.setTrack (track); };
    strips.onEffectDropped = [this] (int track, EffectSlot fx) {
        strips.selectTrack (track);
        effects.setTrack (track);
        if (! addEffectToSelected (std::move (fx)))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, tr ("Plugin database"),
                                                    tr ("The selected insert has no free effect slot."));
        else // FL opens the dropped effect's window (round 171c: the Plugin picker's drag is FL's Browser drag)
            openSlotEditor (track, (int) engine.getSong().mixer[(std::size_t) track].effects.size() - 1);
    };
    strips.onInstrumentDropped = [this] (int track, ChannelDef c) {
        selectTrack (track);
        if (onInstrumentDropped != nullptr)
            onInstrumentDropped (track, std::move (c));
    };
    effects.onInstrumentDropped = strips.onInstrumentDropped;
}

bool MixerComponent::runAction (const std::string& id)
{
    if (id == "mixer.moveLeft")
        strips.moveSelected (-1);
    else if (id == "mixer.moveRight")
        strips.moveSelected (1);
    else if (id == "mixer.linkChannels")
        strips.linkSelectedChannels (false);
    else if (id == "mixer.linkChannelsFrom")
        strips.linkSelectedChannels (true);
    else if (id == "mixer.selectLinkedChannels")
        strips.selectLinkedChannels();
    else if (id == "mixer.audioEditor")
        return strips.runMenuItem (tr ("Open audio editor"));
    else if (id == "mixer.audioEditorRecord")
        return strips.runMenuItem (tr ("Open audio editor and record"));
    else if (id == "mixer.rename") // FL's mixer menu keys (round 167k): the menu's own items
        return strips.runMenuItem (tr ("Rename and color..."));
    else if (id == "mixer.soloSelected")
        return strips.runMenuItem (tr ("Solo selected track(s)"));
    else if (id == "mixer.altSoloSelected")
        return strips.runMenuItem (tr ("Alt solo selected track(s)"));
    else if (id == "mixer.renderArmed")
        return strips.runMenuItem (tr ("Render armed tracks to wave file(s)..."));
    else if (id == "mixer.renderSelected")
        return strips.runMenuItem (tr ("Render selected tracks to wave file(s)..."));
    else if (id == "mixer.deleteTracks") // FL's Del: the selected mixer tracks, after its question (round 186c)
        strips.deleteSelectedTracks();
    else if (id == "mixer.deleteEffect") // FL: Delete on the selected effect slot (issue #11)
        effects.deleteSlot (effects.getSelectedSlot());
    else
        return false;

    return true;
}

bool MixerComponent::loadPresetOnSelection (const juce::File& file)
{
    return mixerpresets::loadOnTracks (engine, strips.getSelection(), file);
}

bool MixerComponent::addEffectToSelected (EffectSlot slot)
{
    const auto t = strips.getSelectedTrack();
    bool added = false;
    engine.editSong ([t, &slot, &added] (Song& s) {
        if (s.mixer.empty())
            s.mixer = Song::defaultMixer (0);
        if (t < (int) s.mixer.size() && (int) s.mixer[(std::size_t) t].effects.size() < maxEffectSlots)
        {
            s.mixer[(std::size_t) t].effects.push_back (std::move (slot));
            added = true;
        }
    });

    if (added)
    {
        effects.setTrack (t);
        effects.selectSlot ((int) engine.getSong().mixer[(std::size_t) t].effects.size() - 1);
    }

    return added;
}

void MixerComponent::selectFirstTrackWithEffects()
{
    const auto& mixer = engine.getSong().mixer;

    for (int t = 0; t < (int) mixer.size(); ++t)
        if (! mixer[(std::size_t) t].effects.empty())
        {
            strips.selectTrack (t);
            return;
        }
}

void MixerComponent::selectTrack (int track)
{
    strips.selectTrack (track);

    // scroll the strip into view
    const auto t = strips.getSelectedTrack();
    const auto x = strips.stripLeft (t);
    const auto w = strips.widthOf (t);
    const auto view = viewport.getViewArea();

    if (x < view.getX() || x + w > view.getRight())
        viewport.setViewPosition (juce::jmax (0, x - view.getWidth() / 2), 0);
}

void MixerComponent::resized()
{
    auto area = getLocalBounds();
    effectsView.setBounds (area.removeFromRight (effectsWidth));
    effects.fitToView (effectsView.getWidth(), effectsView.getHeight(), effectsView.getScrollBarThickness());
    viewport.setBounds (area);
    strips.setVisibleHeight (area.getHeight() - viewport.getScrollBarThickness());
}
} // namespace lynx
