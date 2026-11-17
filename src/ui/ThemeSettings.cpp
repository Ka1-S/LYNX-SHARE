#include "ui/ThemeSettings.h"

#include <array>
#include <map>

#include <juce_audio_formats/juce_audio_formats.h>

#include "platform/Paths.h"
#include "ui/ColourPicker.h"
#include "ui/FileChoosers.h"
#include "ui/I18n.h"
#include "ui/SettingsPages.h"
#include "ui/Theme.h"
#include "ui/WindowOwner.h"

namespace lynx::themeui
{
namespace
{
themes::Spec& state()
{
    static themes::Spec spec;
    return spec;
}

juce::File savedFile()
{
    return platform::settingsFile ("theme" + juce::String (fileExtension));
}

// Colours that components set on themselves from the old tokens (setColour (…, theme::colour::accent)) follow the
// new theme: every colour property equal to an old token becomes the matching new one.
void remapComponentColours (juce::Component& c, const std::map<std::uint32_t, std::uint32_t>& remap)
{
    std::vector<std::pair<int, juce::Colour>> changes;
    const auto& props = c.getProperties();
    for (int i = 0; i < props.size(); ++i)
    {
        const auto name = props.getName (i).toString();
        if (! name.startsWith ("jcclr_"))
            continue;
        const auto argb = (std::uint32_t) (int) props.getValueAt (i);
        if (auto it = remap.find (argb); it != remap.end())
            changes.emplace_back (name.substring (6).getHexValue32(), juce::Colour (it->second));
    }
    for (const auto& [id, colour] : changes)
        c.setColour (id, colour);
    for (auto* child : c.getChildren())
        remapComponentColours (*child, remap);
}

void refreshOpenWindows (const themes::Palette& before, const themes::Palette& after)
{
    if (auto* look = dynamic_cast<theme::LookAndFeel*> (&juce::LookAndFeel::getDefaultLookAndFeel()))
        look->refreshColours();

    std::map<std::uint32_t, std::uint32_t> remap;
    const auto* a = reinterpret_cast<const std::uint32_t*> (&before);
    const auto* b = reinterpret_cast<const std::uint32_t*> (&after);
    static_assert (sizeof (themes::Palette) % sizeof (std::uint32_t) == 0);
    for (std::size_t i = 0; i < sizeof (themes::Palette) / sizeof (std::uint32_t); ++i)
        if (a[i] != b[i])
            remap.emplace (a[i], b[i]); // (a value two tokens share maps to the first one's new value)

    auto& desktop = juce::Desktop::getInstance();
    for (int i = 0; i < desktop.getNumComponents(); ++i)
        if (auto* top = desktop.getComponent (i))
        {
            if (! remap.empty())
                remapComponentColours (*top, remap);
            applyWindowChrome (*top);
            top->sendLookAndFeelChange();
            top->repaint();
        }
}

// the workspace image, decoded once and scaled to the area it was last drawn in
struct Backdrop
{
    juce::String path;
    juce::Image source, scaled;
    juce::Rectangle<int> scaledFor;
    themes::ImageFit scaledFit = themes::ImageFit::fill;
    float scaledOpacity = -1.0f, scaledDim = -1.0f;
    std::uint32_t scaledColour = 0;
};

Backdrop& backdrop()
{
    static Backdrop b;
    return b;
}
} // namespace

const themes::Spec& current()
{
    return state();
}

juce::ChangeBroadcaster& changes()
{
    static juce::ChangeBroadcaster broadcaster;
    return broadcaster;
}

// The colour dots that are drawing colours, not palette tokens (round 190jp, design190ia FW-20): Option = the secondary
// value colour (info), Steps = the lit step, Meters = the meters' green, Waves = the audio clips' and waveforms' colour;
// Selection also sets the selection outline. 0 = the built-in colour (as Theme.h ships it).
void applyDrawingDots (const themes::Spec& spec)
{
    using namespace theme;
    static const auto info = colour::info, meterLow = colour::meterLow, audioClip = colour::audioClip; // (the built-in ones)
    const auto dot = [&spec] (themes::Dot d, juce::Colour fallback) {
        const auto c = spec.dots[(std::size_t) d];
        return c != 0 ? juce::Colour (c | 0xff000000u) : fallback;
    };
    colour::info = dot (themes::Dot::option, info);
    colour::meterLow = dot (themes::Dot::meters, meterLow);
    colour::audioClip = dot (themes::Dot::waves, audioClip);
    colour::stepOn = theme::stepOn = dot (themes::Dot::steps, colour::accent);
    colour::selection = dot (themes::Dot::selection, colour::accent);
}

void apply (const themes::Spec& spec, bool save)
{
    const auto before = theme::currentPalette();
    state() = spec;
    const auto after = themes::adjustedPalette (spec); // (round 190jp: with the Adjustments and the colour dots)
    theme::setPalette (after);
    applyDrawingDots (spec);
    theme::menus::setStyle (spec.menuStyle);
    refreshOpenWindows (before, after);
    if (save)
    {
        if (spec == themes::Spec {})
            savedFile().deleteFile(); // the default look: nothing to keep
        else
            saveFile (spec, savedFile());
    }
    changes().sendChangeMessage();
}

void loadSaved()
{
    if (auto spec = loadFile (savedFile()))
    {
        state() = *spec;
        theme::setPalette (themes::adjustedPalette (*spec));
        applyDrawingDots (*spec);
        theme::menus::setStyle (spec->menuStyle);
        if (auto* look = dynamic_cast<theme::LookAndFeel*> (&juce::LookAndFeel::getDefaultLookAndFeel()))
            look->refreshColours();
    }
}

juce::String toJson (const themes::Spec& spec)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("format", "LYNX theme");
    o->setProperty ("version", 1);
    o->setProperty ("name", juce::String::fromUTF8 (spec.name.c_str()));
    o->setProperty ("accent", juce::String (themes::toHex (spec.accent)));
    o->setProperty ("background", juce::String (themes::toHex (spec.background)));
    o->setProperty ("workspace", spec.workspace == 0 ? juce::String ("auto") : juce::String (themes::toHex (spec.workspace)));
    o->setProperty ("image", juce::String::fromUTF8 (spec.image.c_str()));
    o->setProperty ("fit", juce::String (themes::imageFitId (spec.fit)));
    o->setProperty ("imageOpacity", spec.imageOpacity);
    o->setProperty ("dim", spec.dim);
    if (! spec.startupSound.empty())
        o->setProperty ("startupSound", juce::String::fromUTF8 (spec.startupSound.c_str()));
    // round 190jp (FW-20): the Adjustments and the colour dots, only when set (older files read as before)
    for (auto [key, value] : { std::pair<const char*, float> { "hue", spec.hue }, { "saturation", spec.saturation }, { "brightness", spec.brightness },
                               { "contrast", spec.contrast }, { "textAdjust", spec.text } })
        if (value != 0.0f)
            o->setProperty (key, value);
    if (spec.hasDots())
    {
        auto* dots = new juce::DynamicObject();
        for (int i = 0; i < themes::dotCount; ++i)
            if (spec.dots[(std::size_t) i] != 0)
                dots->setProperty (themes::dotId ((themes::Dot) i), juce::String (themes::toHex (spec.dots[(std::size_t) i])));
        o->setProperty ("dots", juce::var (dots));
    }
    if (spec.gridIntensity != 1.0f) // (round 190je; files without it keep the theme's own lines)
        o->setProperty ("gridIntensity", spec.gridIntensity);
    if (spec.menuStyle != themes::MenuStyle::lynxDark)
        o->setProperty ("menuStyle", juce::String (themes::menuStyleId (spec.menuStyle)));
    return juce::JSON::toString (juce::var (o));
}

std::optional<themes::Spec> fromJson (const juce::String& json)
{
    const auto v = juce::JSON::parse (json);
    if (! v.isObject())
        return std::nullopt;
    themes::Spec spec;
    std::uint32_t argb = 0;
    if (! themes::parseHex (v["accent"].toString().toStdString(), argb))
        return std::nullopt; // not a theme
    spec.accent = argb;
    if (themes::parseHex (v["background"].toString().toStdString(), argb))
        spec.background = argb;
    if (themes::parseHex (v["workspace"].toString().toStdString(), argb))
        spec.workspace = argb;
    if (v["name"].toString().isNotEmpty())
        spec.name = v["name"].toString().toStdString();
    spec.image = v["image"].toString().toStdString();
    spec.fit = themes::imageFitFromId (v["fit"].toString().toStdString());
    if (! v["imageOpacity"].isVoid())
        spec.imageOpacity = juce::jlimit (0.0f, 1.0f, (float) v["imageOpacity"]);
    if (! v["dim"].isVoid())
        spec.dim = juce::jlimit (0.0f, 1.0f, (float) v["dim"]);
    spec.startupSound = v["startupSound"].toString().toStdString();
    spec.hue = juce::jlimit (-180.0f, 180.0f, (float) v.getProperty ("hue", 0.0f));
    spec.saturation = juce::jlimit (-1.0f, 1.0f, (float) v.getProperty ("saturation", 0.0f));
    spec.brightness = juce::jlimit (-1.0f, 1.0f, (float) v.getProperty ("brightness", 0.0f));
    spec.contrast = juce::jlimit (-1.0f, 1.0f, (float) v.getProperty ("contrast", 0.0f));
    spec.text = juce::jlimit (-1.0f, 1.0f, (float) v.getProperty ("textAdjust", 0.0f));
    if (const auto dots = v["dots"]; dots.isObject())
        for (int i = 0; i < themes::dotCount; ++i)
            if (themes::parseHex (dots[themes::dotId ((themes::Dot) i)].toString().toStdString(), argb))
                spec.dots[(std::size_t) i] = argb | 0xff000000u;
    if (! v["gridIntensity"].isVoid())
        spec.gridIntensity = (float) juce::jlimit (themes::gridIntensityMin, themes::gridIntensityMax, (double) v["gridIntensity"]);
    spec.menuStyle = themes::menuStyleFromId (v["menuStyle"].toString().toStdString());
    return spec;
}

bool saveFile (const themes::Spec& spec, const juce::File& file)
{
    file.getParentDirectory().createDirectory();
    return file.replaceWithText (toJson (spec));
}

std::optional<themes::Spec> loadFile (const juce::File& file)
{
    if (! file.existsAsFile() || file.getSize() > 64 * 1024)
        return std::nullopt;
    return fromJson (file.loadFileAsString());
}

juce::File userThemesFolder()
{
    return platform::settingsFile ("Themes");
}

std::function<void (const juce::String&)> previewStartupSound;

juce::File backgroundsFolder()
{
    static const juce::File folder = [] {
        if (const auto profile = platform::profileOverride(); profile != juce::File())
            return profile.getChildFile ("Backgrounds");
        // (a probe file: Windows reports Program Files as writable and then virtualises the writes)
        const auto beside = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getSiblingFile ("Backgrounds");
        if (beside.isDirectory() || beside.createDirectory().wasOk())
        {
            const auto probe = beside.getChildFile (".lynx-write-test");
            const auto ok = probe.replaceWithText ("x") && probe.existsAsFile();
            probe.deleteFile();
            if (ok)
                return beside;
        }
        return platform::settingsFile ("Backgrounds");
    }();
    folder.createDirectory();
    return folder;
}

juce::File imageFile (const std::string& image)
{
    if (image.empty())
        return {};
    const auto path = juce::String::fromUTF8 (image.c_str());
    return juce::File::isAbsolutePath (path) ? juce::File (path) : backgroundsFolder().getChildFile (path);
}

std::string imagePathFor (const juce::File& file)
{
    const auto folder = backgroundsFolder();
    if (file.isAChildOf (folder))
        return file.getRelativePathFrom (folder).replaceCharacter ('\\', '/').toStdString();
    return file.getFullPathName().toStdString();
}

void paintWorkspaceBackdrop (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto& spec = state();
    const auto colour = juce::Colour (themes::workspaceColour (spec));
    g.setColour (colour);
    g.fillRect (area);
    if (spec.image.empty() || area.isEmpty())
        return;

    auto& b = backdrop();
    const auto file = imageFile (spec.image);
    const auto path = file.getFullPathName();
    if (b.path != path)
    {
        b = {};
        b.path = path;
        if (file.existsAsFile() && file.getSize() < 64 * 1024 * 1024)
            b.source = juce::ImageFileFormat::loadFrom (file);
    }
    if (! b.source.isValid())
        return;

    // the image as it is drawn, made again only when the area or the settings change (a big photo scaled on every
    // repaint would cost the CPU renderers a lot)
    if (b.scaledFor != area || b.scaledFit != spec.fit || b.scaledOpacity != spec.imageOpacity || b.scaledDim != spec.dim
        || b.scaledColour != colour.getARGB() || ! b.scaled.isValid())
    {
        b.scaledFor = area;
        b.scaledFit = spec.fit;
        b.scaledOpacity = spec.imageOpacity;
        b.scaledDim = spec.dim;
        b.scaledColour = colour.getARGB();
        b.scaled = juce::Image (juce::Image::ARGB, juce::jmax (1, area.getWidth()), juce::jmax (1, area.getHeight()), true);
        juce::Graphics ig (b.scaled);
        ig.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        const auto local = b.scaled.getBounds().toFloat();
        const auto src = b.source.getBounds().toFloat();
        ig.setOpacity (juce::jlimit (0.0f, 1.0f, spec.imageOpacity));
        switch (spec.fit)
        {
            case themes::ImageFit::fill:
            {
                const auto scale = juce::jmax (local.getWidth() / src.getWidth(), local.getHeight() / src.getHeight());
                ig.drawImageTransformed (b.source,
                                         juce::AffineTransform::scale (scale).translated ((local.getWidth() - src.getWidth() * scale) * 0.5f,
                                                                                          (local.getHeight() - src.getHeight() * scale) * 0.5f));
                break;
            }
            case themes::ImageFit::fit:
                ig.drawImageWithin (b.source, 0, 0, b.scaled.getWidth(), b.scaled.getHeight(), juce::RectanglePlacement::centred);
                break;
            case themes::ImageFit::stretch:
                ig.drawImageWithin (b.source, 0, 0, b.scaled.getWidth(), b.scaled.getHeight(), juce::RectanglePlacement::stretchToFit);
                break;
            case themes::ImageFit::tile:
                ig.setTiledImageFill (b.source, 0, 0, juce::jlimit (0.0f, 1.0f, spec.imageOpacity));
                ig.fillAll();
                break;
            case themes::ImageFit::centre:
                ig.drawImageAt (b.source, (b.scaled.getWidth() - b.source.getWidth()) / 2, (b.scaled.getHeight() - b.source.getHeight()) / 2);
                break;
        }
        if (spec.dim > 0.0f)
        {
            ig.setOpacity (1.0f);
            ig.setColour (colour.withAlpha (juce::jlimit (0.0f, 1.0f, spec.dim)));
            ig.fillAll();
        }
    }
    g.drawImageAt (b.scaled, area.getX(), area.getY());
}

// --- Options › Theme ---------------------------------------------------------------------------------------------------
namespace
{
using namespace settingsui;

// a colour well: the colour, its name and hex; the selected one is edited by the picker below
class ColourWell final : public juce::Button
{
public:
    explicit ColourWell (juce::String label) : juce::Button (label) { setClickingTogglesState (false); }

    juce::Colour colour;
    juce::String note; // e.g. "automatic"

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        using namespace theme;
        const auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (getToggleState() ? colour::bg5 : over ? colour::bg4 : colour::bg3);
        g.fillRoundedRectangle (r, radius::md);
        if (getToggleState())
        {
            g.setColour (colour::accent);
            g.drawRoundedRectangle (r.reduced (0.5f), radius::md, 1.5f);
        }
        auto inner = getLocalBounds().reduced (8, 6);
        const auto swatch = inner.removeFromLeft (inner.getHeight()).toFloat();
        g.setColour (this->colour);
        g.fillRoundedRectangle (swatch, radius::sm);
        g.setColour (colour::borderStrong);
        g.drawRoundedRectangle (swatch, radius::sm, 1.0f);
        inner.removeFromLeft (8);
        g.setColour (colour::textPrimary);
        g.setFont (font (type::body, true));
        g.drawText (getButtonText(), inner.removeFromTop (inner.getHeight() / 2), juce::Justification::bottomLeft, true);
        g.setColour (colour::textSecondary);
        g.setFont (monoFont (type::small));
        g.drawText (note.isNotEmpty() ? note : juce::String (themes::toHex (this->colour.getARGB())), inner, juce::Justification::topLeft, true);
    }
};

// --- FL's Theme page landmarks (round 190jp, design190ia slice 26 FW-S5b / FW-20) -----------------------------------------
// A preset's thumbnail: the user's preset keeps the 1/10 picture of the window taken when it was saved (a PNG beside its
// .lynxtheme); a built-in preset (or one saved without a window) gets a small drawing of LYNX's layout in its colours.
// Both are made once and kept: nothing is drawn per paint.
juce::Image schematicThumbnail (const themes::Spec& spec, int w, int h)
{
    const auto p = themes::adjustedPalette (spec);
    const auto c = [] (std::uint32_t argb) { return juce::Colour (argb); };
    juce::Image image (juce::Image::ARGB, w, h, true);
    juce::Graphics g (image);
    const auto fw = (float) w, fh = (float) h;
    g.fillAll (c (themes::workspaceColour (spec)));
    // toolbar: the transport (an accent play), panel buttons
    const auto bar = fh * 0.13f;
    g.setColour (c (p.bg3));
    g.fillRect (0.0f, 0.0f, fw, bar);
    g.setColour (c (p.accent));
    g.fillEllipse (fw * 0.30f, bar * 0.28f, bar * 0.44f, bar * 0.44f);
    g.setColour (c (p.bg5));
    for (int i = 0; i < 6; ++i)
        g.fillRect (fw * (0.58f + 0.06f * (float) i), bar * 0.3f, fw * 0.04f, bar * 0.4f);
    // browser
    g.setColour (c (p.bg2));
    g.fillRect (0.0f, bar + 1.0f, fw * 0.15f, fh - bar - 1.0f);
    g.setColour (c (p.textSecondary).withAlpha (0.45f));
    for (int i = 0; i < 7; ++i)
        g.fillRect (fw * 0.02f, bar + 6.0f + (float) i * fh * 0.07f, fw * (0.07f + 0.01f * (float) (i % 3)), 1.5f);
    // channel rack: name buttons and steps, a few lit
    const auto rack = juce::Rectangle<float> (fw * 0.17f, bar + fh * 0.05f, fw * 0.36f, fh * 0.48f);
    g.setColour (c (p.bg2));
    g.fillRect (rack);
    for (int row = 0; row < 5; ++row)
    {
        const auto y = rack.getY() + 4.0f + (float) row * rack.getHeight() * 0.18f;
        g.setColour (c (p.bg4));
        g.fillRect (rack.getX() + 3.0f, y, rack.getWidth() * 0.22f, rack.getHeight() * 0.12f);
        for (int step = 0; step < 8; ++step)
        {
            const auto lit = (step + row * 3) % 5 == 0;
            g.setColour (c (lit ? p.accent : step / 4 % 2 == 0 ? p.stepOff : p.stepOffAlt));
            g.fillRect (rack.getX() + rack.getWidth() * (0.3f + 0.085f * (float) step), y, rack.getWidth() * 0.065f, rack.getHeight() * 0.12f);
        }
    }
    // playlist: rows and clips in the accent's tint
    const auto list = juce::Rectangle<float> (fw * 0.55f, bar + fh * 0.05f, fw * 0.43f, fh * 0.8f);
    g.setColour (c (p.rowWhite));
    g.fillRect (list);
    g.setColour (c (p.gridBar));
    for (int i = 1; i < 4; ++i)
        g.fillRect (list.getX() + list.getWidth() * 0.25f * (float) i, list.getY(), 1.0f, list.getHeight());
    for (int row = 0; row < 5; ++row)
    {
        g.setColour (c (themes::mix (p.accent, p.rowWhite, 0.25 + 0.12 * row)));
        g.fillRect (list.getX() + list.getWidth() * (0.04f + 0.1f * (float) (row % 3)), list.getY() + 4.0f + (float) row * list.getHeight() * 0.19f,
                    list.getWidth() * 0.45f, list.getHeight() * 0.13f);
    }
    // mixer strips under the rack
    const auto mixer = juce::Rectangle<float> (fw * 0.17f, rack.getBottom() + fh * 0.05f, fw * 0.36f, fh - rack.getBottom() - fh * 0.09f);
    g.setColour (c (p.bg2));
    g.fillRect (mixer);
    for (int i = 0; i < 7; ++i)
    {
        g.setColour (c (p.bg4));
        g.fillRect (mixer.getX() + 3.0f + (float) i * mixer.getWidth() * 0.135f, mixer.getY() + 3.0f, mixer.getWidth() * 0.1f, mixer.getHeight() - 6.0f);
        g.setColour (theme::colour::meterLow);
        const auto level = 0.3f + 0.08f * (float) ((i * 5) % 7);
        g.fillRect (mixer.getX() + 4.0f + (float) i * mixer.getWidth() * 0.135f, mixer.getBottom() - 3.0f - (mixer.getHeight() - 6.0f) * level, 2.0f,
                    (mixer.getHeight() - 6.0f) * level);
    }
    return image;
}

juce::File thumbnailFileFor (const juce::File& presetFile) { return presetFile.withFileExtension ("png"); }

// The preset grid: FL's three columns of thumbnails with the names under them; the chosen one framed in the accent.
class PresetGrid final : public juce::Component
{
public:
    struct Item
    {
        juce::String name;
        themes::Spec spec;
        juce::File file; // a user's preset (none: built in)
        juce::Image thumbnail;
    };

    std::function<void (int)> onPick;
    static constexpr int columns = 3, gap = 12, nameHeight = 20;

    // (the thumbnails are made only for items that are new to the grid: an edit that rebuilds the list reuses them)
    void setItems (std::vector<Item> items)
    {
        for (auto& item : items)
        {
            const auto key = (item.file == juce::File() ? "builtin:" + item.name : item.file.getFullPathName() + ":"
                                                                                       + juce::String (item.file.getLastModificationTime().toMilliseconds()));
            if (auto it = cache.find (key); it != cache.end())
                item.thumbnail = it->second;
            else
            {
                const auto png = item.file == juce::File() ? juce::File() : thumbnailFileFor (item.file);
                item.thumbnail = png.existsAsFile() ? juce::ImageFileFormat::loadFrom (png) : juce::Image();
                if (! item.thumbnail.isValid())
                    item.thumbnail = schematicThumbnail (item.spec, 184, 104);
                cache[key] = item.thumbnail;
            }
        }
        list = std::move (items);
        repaint();
    }
    void setChosen (int index)
    {
        chosen = index;
        repaint();
    }
    int heightFor (int width) const
    {
        const auto rows = ((int) list.size() + columns - 1) / columns;
        return rows * (thumbHeight (width) + nameHeight + gap);
    }

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        for (int i = 0; i < (int) list.size(); ++i)
        {
            const auto cell = cellOf (i);
            const auto thumb = cell.withHeight (thumbHeight (getWidth()));
            g.drawImageWithin (list[(std::size_t) i].thumbnail, thumb.getX(), thumb.getY(), thumb.getWidth(), thumb.getHeight(),
                               juce::RectanglePlacement::fillDestination);
            const auto on = i == chosen, over = i == hovered;
            g.setColour (on ? colour::accent : over ? colour::textSecondary : colour::borderStrong);
            g.drawRect (thumb.expanded (on ? 2 : 1), on ? 2 : 1);
            g.setColour (on ? colour::textPrimary : colour::textSecondary);
            g.setFont (font (type::body, on));
            g.drawFittedText (list[(std::size_t) i].name, cell.withTop (thumb.getBottom() + 3).withHeight (nameHeight - 3), juce::Justification::centred, 1, 0.8f);
        }
    }
    void mouseMove (const juce::MouseEvent& e) override { setHovered (indexAt (e.getPosition())); }
    void mouseExit (const juce::MouseEvent&) override { setHovered (-1); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (const auto i = indexAt (e.getPosition()); i >= 0 && e.mouseWasClicked() && onPick)
            onPick (i);
    }
    const std::vector<Item>& items() const { return list; }

private:
    static int thumbWidth (int width) { return (width - (columns - 1) * gap) / columns; }
    static int thumbHeight (int width) { return thumbWidth (width) * 104 / 184; } // (FL's 184 x 104)
    juce::Rectangle<int> cellOf (int i) const
    {
        const auto w = thumbWidth (getWidth()), h = thumbHeight (getWidth()) + nameHeight;
        return { (i % columns) * (w + gap), (i / columns) * (h + gap) + 2, w, h };
    }
    int indexAt (juce::Point<int> p) const
    {
        for (int i = 0; i < (int) list.size(); ++i)
            if (cellOf (i).contains (p))
                return i;
        return -1;
    }
    void setHovered (int i)
    {
        if (i != hovered)
        {
            hovered = i;
            repaint();
        }
    }

    std::vector<Item> list;
    std::map<juce::String, juce::Image> cache;
    int chosen = -1, hovered = -1;
};

// FL's colour dots: a Ø31 colour with a dark ring and its name under it; the one the picker edits ringed in the accent.
class DotButton final : public juce::Button
{
public:
    explicit DotButton (const juce::String& name) : juce::Button (name) {}
    juce::Colour colour;
    bool automatic = true;

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        using namespace theme;
        const auto d = 31.0f;
        const auto circle = juce::Rectangle<float> ((float) getWidth() / 2.0f - d / 2.0f, 2.0f, d, d);
        g.setColour (this->colour);
        g.fillEllipse (circle);
        g.setColour (juce::Colours::black);
        g.drawEllipse (circle, 1.0f);
        if (getToggleState() || over)
        {
            g.setColour (getToggleState() ? colour::accent : colour::textSecondary);
            g.drawEllipse (circle.expanded (2.5f), getToggleState() ? 2.0f : 1.0f);
        }
        g.setColour (getToggleState() ? colour::textPrimary : colour::textSecondary);
        g.setFont (font (type::small, ! automatic)); // (a colour of the user's own: its name in bold)
        g.drawFittedText (getButtonText(), getLocalBounds().withTop ((int) circle.getBottom() + 4), juce::Justification::centredTop, 1, 0.75f);
    }
};

class ThemePage final : public ScrollingPage, private juce::Timer
{
public:
    ThemePage() : spec (current())
    {
        for (auto* c : std::initializer_list<juce::Component*> { &presetTitle, &presetGrid, &importButton, &exportButton, &savePreset, &resetButton,
                                                                 &presetNote, &adjustTitle, &adjustNote, &coloursTitle, &accentWell, &backgroundWell,
                                                                 &workspaceWell, &autoWorkspace, &picker, &contrastNote, &imageTitle, &imageButton,
                                                                 &removeImage, &imageName, &fitLabel, &fitBox, &opacityLabel, &opacitySlider, &dimLabel,
                                                                 &dimSlider, &imageListLabel, &imageList, &openBackgrounds, &imageNote, &soundTitle,
                                                                 &soundName, &soundButton, &soundPlay, &soundDefault, &soundNote, &slotA, &slotB, &abNote, &lookTitle,
                                                                 &menuStyleLabel, &menuStyleBox, &gridLabel, &gridSlider, &lookNote })
            add (*c);

        // round 190je: the menus' palette (FL's classic light grey as an option, design190ia FW-16) and how strongly the
        // editors' grid lines show (Live's "Grid Line Intensity", FM-18); presets leave both alone
        styleLabel (menuStyleLabel, tr ("Menu style"));
        menuStyleBox.addItem (tr ("LYNX dark (default)"), 1);
        menuStyleBox.addItem (tr ("FL classic (light)"), 2);
        menuStyleBox.onChange = [this] {
            edit ([style = menuStyleBox.getSelectedId() == 2 ? themes::MenuStyle::flClassic : themes::MenuStyle::lynxDark] (themes::Spec& s) { s.menuStyle = style; });
        };
        styleLabel (gridLabel, tr ("Grid line intensity"));
        gridSlider.setRange (themes::gridIntensityMin * 100.0, themes::gridIntensityMax * 100.0, 5.0);
        gridSlider.setTextValueSuffix ("%");
        gridSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 20);
        gridSlider.setDoubleClickReturnValue (true, 100.0);
        gridSlider.onValueChange = [this] { edit ([v = (float) gridSlider.getValue() / 100.0f] (themes::Spec& s) { s.gridIntensity = v; }, false); };
        styleNote (lookNote, tr ("Grid line intensity: the bar, beat and step lines of the Piano roll, Playlist and Channel rack (100 % = the theme's own; double-click resets)."));

        // the Backgrounds folder next to LYNX.exe (issue #12): its images in a list, the folder a click away
        styleLabel (imageListLabel, tr ("Backgrounds folder"));
        imageList.onChange = [this] {
            const auto i = imageList.getSelectedItemIndex();
            if (i >= 0 && i < backgroundFiles.size())
                edit ([path = imagePathFor (backgroundFiles[i])] (themes::Spec& s) { s.image = path; });
        };
        openBackgrounds.onClick = [] { backgroundsFolder().startAsProcess(); };
        openBackgrounds.setTooltip (tr ("Open the Backgrounds folder: images put there show in the list (refreshed when this page opens)"));
        styleNote (imageNote, tr ("Images you choose are copied into the Backgrounds folder next to LYNX.exe, so a portable LYNX SHARE keeps them, "
                                  "and a saved theme preset keeps its image."));

        // Startup sound (issue #12): any audio file instead of LYNX's own sound
        soundButton.onClick = [this] { chooseSound(); };
        soundPlay.onClick = [this] {
            if (previewStartupSound)
                previewStartupSound (juce::String::fromUTF8 (spec.startupSound.c_str()));
        };
        soundDefault.onClick = [this] { edit ([] (themes::Spec& s) { s.startupSound.clear(); }); };
        styleNote (soundNote, tr ("Played when LYNX SHARE starts (if Settings › General › \"Play the startup sound\" is on). Default: LYNX SHARE's own sound."));

        // round 190jp (design190ia FW-20): FL's preset thumbnails instead of a list
        presetGrid.onPick = [this] (int index) { choosePreset (index); };
        importButton.onClick = [this] { importTheme(); };
        exportButton.onClick = [this] { exportTheme (false); };
        savePreset.onClick = [this] { exportTheme (true); };
        resetButton.onClick = [this] { edit ([] (themes::Spec& s) { s = themes::Spec {}; }); };
        styleNote (presetNote, tr ("Themes change the accent color, the background and the workspace behind the panels. Your own presets are "
                                   "saved as .lynxtheme files and can be shared."));
        importButton.setTooltip (tr ("Load a .lynxtheme file"));
        exportButton.setTooltip (tr ("Save the current theme as a .lynxtheme file"));
        savePreset.setTooltip (tr ("Keep the current theme in the preset list"));

        // FL's Adjustments: five knobs over the whole palette (double-click: back to 0), applied a moment after a move
        const juce::String knobNames[] = { tr ("Hue"), tr ("Saturation"), tr ("Brightness"), tr ("Contrast"), tr ("Text") };
        for (int i = 0; i < knobCount; ++i)
        {
            auto& k = knobs[(std::size_t) i];
            k.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            k.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            k.setRange (i == 0 ? -180.0 : -100.0, i == 0 ? 180.0 : 100.0, 1.0);
            k.setDoubleClickReturnValue (true, 0.0);
            k.setTextValueSuffix (i == 0 ? juce::String::fromUTF8 ("\xc2\xb0") : juce::String ("%"));
            k.setPopupDisplayEnabled (true, true, this);
            k.setTitle (knobNames[i]);
            k.onValueChange = [this, i] {
                const auto v = (float) knobs[(std::size_t) i].getValue();
                editSoon ([i, v] (themes::Spec& s) {
                    switch (i)
                    {
                        case 0: s.hue = v; break;
                        case 1: s.saturation = v / 100.0f; break;
                        case 2: s.brightness = v / 100.0f; break;
                        case 3: s.contrast = v / 100.0f; break;
                        default: s.text = v / 100.0f; break;
                    }
                });
            };
            auto& l = knobLabels[(std::size_t) i];
            l.setText (knobNames[i], juce::dontSendNotification);
            l.setJustificationType (juce::Justification::centredTop);
            l.setFont (theme::font (theme::type::small));
            l.setColour (juce::Label::textColourId, theme::colour::textSecondary);
            l.setMinimumHorizontalScale (0.75f);
            add (k);
            add (l);
        }
        // FL's colour dots: a click lets the picker below edit that colour, a double-click gives it back to the theme
        const juce::String dotNames[] = { tr ("Selection"), tr ("Highlight"), tr ("Mute"), tr ("Option"), tr ("Steps"), tr ("Meters"), tr ("Waves"), tr ("Text") };
        for (int i = 0; i < themes::dotCount; ++i)
        {
            auto& d = dots[(std::size_t) i];
            d.setButtonText (dotNames[i]);
            d.setTooltip (tr ("Click: edit this color with the picker. Double-click: the theme's own color."));
            d.onClick = [this, i] { selectDot (i); };
            d.addMouseListener (this, false);
            add (d);
        }
        styleNote (adjustNote, tr ("Adjustments shift the whole theme. The dots set single colors: click one to edit it below."));

        for (auto* w : { &accentWell, &backgroundWell, &workspaceWell })
            w->onClick = [this, w] { select (w); };
        autoWorkspace.setButtonText (tr ("Automatic workspace color (follows the background)"));
        autoWorkspace.onClick = [this] {
            const auto on = autoWorkspace.getToggleState();
            edit ([on] (themes::Spec& s) { s.workspace = on ? 0u : themes::workspaceColour (s); });
        };
        picker.onChange = [this] (juce::Colour c) {
            const auto argb = c.getARGB();
            edit ([this, argb] (themes::Spec& s) {
                if (selectedDot >= 0)
                    s.dots[(std::size_t) selectedDot] = argb | 0xff000000u;
                else if (selected == &accentWell)
                    s.accent = argb;
                else if (selected == &backgroundWell)
                    s.background = argb;
                else
                    s.workspace = argb;
            }, false);
        };

        // FL's A | B: two versions of the theme to compare (edits go to the lit one; the other keeps its state)
        slotA.setButtonText ("A");
        slotB.setButtonText ("B");
        slotA.setConnectedEdges (juce::Button::ConnectedOnRight);
        slotB.setConnectedEdges (juce::Button::ConnectedOnLeft);
        for (auto* b : { &slotA, &slotB })
        {
            b->setClickingTogglesState (false);
            b->setColour (juce::TextButton::buttonOnColourId, theme::colour::accent);
            b->setColour (juce::TextButton::textColourOnId, theme::colour::onAccent);
        }
        slotA.setTooltip (tr ("Theme A: compare two versions of the theme"));
        slotB.setTooltip (tr ("Theme B: compare two versions of the theme"));
        slotA.onClick = [this] { switchSlot (0); };
        slotB.onClick = [this] { switchSlot (1); };
        slots = { spec, spec };
        soon.fire = [this] {
            soonPending = false;
            apply (spec, false);
            refresh();
            startTimer (600);
        };
        styleNote (abNote, tr ("A and B keep two versions of the theme while this page is open: switch to compare them."));

        imageButton.onClick = [this] { chooseImage(); };
        removeImage.onClick = [this] { edit ([] (themes::Spec& s) { s.image.clear(); }); };
        styleLabel (fitLabel, tr ("Image fit"));
        const juce::String fits[] = { tr ("Fill (crop to cover)"), tr ("Fit (whole image)"), tr ("Stretch"), tr ("Tile"), tr ("Center") };
        for (int i = 0; i < themes::imageFitCount; ++i)
            fitBox.addItem (fits[i], i + 1);
        fitBox.onChange = [this] { edit ([this] (themes::Spec& s) { s.fit = (themes::ImageFit) juce::jlimit (0, themes::imageFitCount - 1, fitBox.getSelectedId() - 1); }); };
        styleLabel (opacityLabel, tr ("Image opacity"));
        styleLabel (dimLabel, tr ("Dim"));
        for (auto* s : { &opacitySlider, &dimSlider })
        {
            s->setRange (0.0, 100.0, 1.0);
            s->setTextValueSuffix ("%");
            s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 20);
        }
        opacitySlider.onValueChange = [this] { edit ([v = (float) opacitySlider.getValue() / 100.0f] (themes::Spec& s) { s.imageOpacity = v; }, false); };
        dimSlider.onValueChange = [this] { edit ([v = (float) dimSlider.getValue() / 100.0f] (themes::Spec& s) { s.dim = v; }, false); };
        styleNote (imageName, {});

        refresh();
        select (&accentWell);
    }

    ~ThemePage() override
    {
        soon.stopTimer();
        if (isTimerRunning() || soonPending) // a change still waiting for its save
            flush();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        for (int i = 0; i < themes::dotCount; ++i)
            if (e.eventComponent == &dots[(std::size_t) i])
            {
                edit ([i] (themes::Spec& s) { s.dots[(std::size_t) i] = 0; });
                selectDot (i);
            }
    }

private:
    static constexpr int knobCount = 5;

    int layout (int width) override
    {
        auto area = juce::Rectangle<int> (0, 0, width, 10000).reduced (20, 14);
        const auto top = area.getY();
        auto row = [&area] (int h) {
            auto r = area.removeFromTop (h);
            area.removeFromTop (6);
            return r;
        };
        presetTitle.setBounds (row (24));
        presetGrid.setBounds (row (presetGrid.heightFor (area.getWidth())));
        {
            auto r = row (28);
            const auto w = (r.getWidth() - 12) / 4;
            for (auto* b : { &importButton, &exportButton, &savePreset, &resetButton })
            {
                b->setBounds (r.removeFromLeft (w));
                r.removeFromLeft (4);
            }
        }
        presetNote.setBounds (row (32));

        // Adjustments: the knobs, then the dots (FL: 61 px apart)
        adjustTitle.setBounds (row (24));
        {
            auto r = row (64);
            for (int i = 0; i < knobCount; ++i)
            {
                auto cell = r.removeFromLeft (64);
                knobs[(std::size_t) i].setBounds (cell.removeFromTop (44).withSizeKeepingCentre (40, 40));
                knobLabels[(std::size_t) i].setBounds (cell);
            }
        }
        {
            auto r = row (56);
            const auto pitch = juce::jmin (64, r.getWidth() / themes::dotCount);
            for (auto& d : dots)
                d.setBounds (r.removeFromLeft (pitch));
        }
        adjustNote.setBounds (row (18));

        coloursTitle.setBounds (row (24));
        {
            auto r = row (44);
            const auto w = (r.getWidth() - 16) / 3;
            accentWell.setBounds (r.removeFromLeft (w));
            r.removeFromLeft (8);
            backgroundWell.setBounds (r.removeFromLeft (w));
            r.removeFromLeft (8);
            workspaceWell.setBounds (r);
        }
        picker.setBounds (row (picker.preferredHeight()).withWidth (juce::jmin (area.getWidth(), 520)));
        autoWorkspace.setBounds (row (24));
        contrastNote.setBounds (row (18));

        imageTitle.setBounds (row (24));
        {
            auto r = row (28);
            imageButton.setBounds (r.removeFromLeft (150));
            r.removeFromLeft (8);
            removeImage.setBounds (r.removeFromLeft (90));
            r.removeFromLeft (10);
            imageName.setBounds (r.withTrimmedTop (6));
        }
        {
            auto r = row (26);
            imageListLabel.setBounds (r.removeFromLeft (130));
            imageList.setBounds (r.removeFromLeft (juce::jmin (300, r.getWidth() - 140)));
            r.removeFromLeft (8);
            openBackgrounds.setBounds (r.removeFromLeft (juce::jmin (130, r.getWidth())));
        }
        imageNote.setBounds (row (32));
        lookTitle.setBounds (row (24));
        for (auto [label, control] : { std::pair<juce::Component*, juce::Component*> { &menuStyleLabel, &menuStyleBox }, { &gridLabel, &gridSlider } })
        {
            auto r = row (26);
            label->setBounds (r.removeFromLeft (130));
            control->setBounds (r.removeFromLeft (juce::jmin (300, r.getWidth())));
        }
        lookNote.setBounds (row (32));
        for (auto [label, control] : { std::pair<juce::Component*, juce::Component*> { &fitLabel, &fitBox }, { &opacityLabel, &opacitySlider }, { &dimLabel, &dimSlider } })
        {
            auto r = row (26);
            label->setBounds (r.removeFromLeft (130));
            control->setBounds (r.removeFromLeft (juce::jmin (300, r.getWidth())));
        }

        soundTitle.setBounds (row (24));
        {
            auto r = row (28);
            soundButton.setBounds (r.removeFromLeft (150));
            r.removeFromLeft (8);
            soundPlay.setBounds (r.removeFromLeft (80));
            r.removeFromLeft (8);
            soundDefault.setBounds (r.removeFromLeft (90));
            r.removeFromLeft (10);
            soundName.setBounds (r.withTrimmedTop (6));
        }
        soundNote.setBounds (row (32));

        // FL's A | B at the bottom left
        area.removeFromTop (6);
        {
            auto r = row (30);
            slotA.setBounds (r.removeFromLeft (44));
            slotB.setBounds (r.removeFromLeft (44));
            r.removeFromLeft (12);
            abNote.setBounds (r.withTrimmedTop (2));
        }
        return area.getY() - top + 28;
    }

    // a change from a control: applied at once (live), saved a moment later
    void edit (const std::function<void (themes::Spec&)>& change, bool refreshPicker = true)
    {
        change (spec);
        apply (spec, false);
        refresh (refreshPicker);
        startTimer (600);
    }

    // a knob's change: applied a moment after the last move (every open window repaints with a new palette)
    void editSoon (const std::function<void (themes::Spec&)>& change)
    {
        change (spec);
        soonPending = true;
        soon.startTimer (60);
    }

    void timerCallback() override { flush(); }

    void flush()
    {
        stopTimer();
        apply (spec, true);
    }

    void select (ColourWell* well)
    {
        selected = well;
        selectedDot = -1;
        for (auto* w : { &accentWell, &backgroundWell, &workspaceWell })
            w->setToggleState (w == well, juce::dontSendNotification);
        for (auto& d : dots)
            d.setToggleState (false, juce::dontSendNotification);
        picker.setOriginal (well->colour);
        picker.setColour (well->colour, false);
    }

    void selectDot (int index)
    {
        selectedDot = index;
        for (auto* w : { &accentWell, &backgroundWell, &workspaceWell })
            w->setToggleState (false, juce::dontSendNotification);
        for (int i = 0; i < themes::dotCount; ++i)
            dots[(std::size_t) i].setToggleState (i == index, juce::dontSendNotification);
        const auto c = dots[(std::size_t) index].colour;
        picker.setOriginal (c);
        picker.setColour (c, false);
    }

    void switchSlot (int slot)
    {
        if (slot == activeSlot)
            return;
        slots[(std::size_t) activeSlot] = spec;
        activeSlot = slot;
        const auto next = slots[(std::size_t) slot];
        edit ([next] (themes::Spec& s) { s = next; });
    }

    // the colour each dot shows: the user's own, else the one the theme draws with
    juce::Colour dotColour (int i, const themes::Palette& palette) const
    {
        if (const auto own = spec.dots[(std::size_t) i]; own != 0)
            return juce::Colour (own | 0xff000000u);
        using D = themes::Dot;
        switch ((D) i)
        {
            case D::selection: return juce::Colour (palette.accent);
            case D::highlight: return juce::Colour (palette.accentHover);
            case D::mute: return juce::Colour (palette.textDisabled);
            case D::option: return theme::colour::info;
            case D::steps: return juce::Colour (palette.accent);
            case D::meters: return theme::colour::meterLow;
            case D::waves: return theme::colour::audioClip;
            case D::text: return juce::Colour (palette.textPrimary);
        }
        return juce::Colour (palette.textPrimary);
    }

    void refresh (bool refreshPicker = true)
    {
        accentWell.colour = juce::Colour (spec.accent);
        backgroundWell.colour = juce::Colour (spec.background);
        workspaceWell.colour = juce::Colour (themes::workspaceColour (spec));
        workspaceWell.note = spec.workspace == 0 ? tr ("Automatic") : juce::String();
        autoWorkspace.setToggleState (spec.workspace == 0, juce::dontSendNotification);
        const auto palette = themes::adjustedPalette (spec);
        for (int i = 0; i < themes::dotCount; ++i)
        {
            dots[(std::size_t) i].colour = dotColour (i, palette);
            dots[(std::size_t) i].automatic = spec.dots[(std::size_t) i] == 0;
            dots[(std::size_t) i].repaint();
        }
        if (refreshPicker && selectedDot >= 0)
            picker.setColour (dots[(std::size_t) selectedDot].colour, false);
        else if (refreshPicker && selected != nullptr)
            picker.setColour (selected->colour, false);
        for (auto* w : { &accentWell, &backgroundWell, &workspaceWell })
            w->repaint();
        const float values[] = { spec.hue, spec.saturation * 100.0f, spec.brightness * 100.0f, spec.contrast * 100.0f, spec.text * 100.0f };
        for (int i = 0; i < knobCount; ++i)
            if (! knobs[(std::size_t) i].isMouseButtonDown())
                knobs[(std::size_t) i].setValue (std::round (values[i]), juce::dontSendNotification);
        slotA.setToggleState (activeSlot == 0, juce::dontSendNotification);
        slotB.setToggleState (activeSlot == 1, juce::dontSendNotification);

        const auto ratio = themes::contrastRatio (palette.onAccent, palette.accent);
        contrastNote.setText (tr ("Text on the accent: {0} (contrast {1}:1). Text on panels: contrast {2}:1.",
                                  { palette.onAccent == 0xffffffff ? tr ("white") : tr ("dark"), juce::String (ratio, 1),
                                    juce::String (themes::contrastRatio (palette.textPrimary, palette.bg2), 1) }),
                              juce::dontSendNotification);
        contrastNote.setFont (theme::font (theme::type::small));
        contrastNote.setColour (juce::Label::textColourId, theme::colour::textSecondary);

        // the presets: built-in, then the user's own (thumbnails made once, kept by the grid)
        std::vector<PresetGrid::Item> items;
        auto chosen = -1;
        for (const auto& t : themes::builtInThemes())
        {
            if (chosen < 0 && t.accent == spec.accent && t.background == spec.background && ! spec.hasAdjustments() && ! spec.hasDots())
                chosen = (int) items.size();
            items.push_back ({ juce::String::fromUTF8 (t.name.c_str()) + (items.empty() ? " " + tr ("(default)") : juce::String()), t, {}, {} });
        }
        auto files = userThemesFolder().findChildFiles (juce::File::findFiles, false, juce::String ("*") + fileExtension);
        files.sort();
        for (const auto& f : files)
            if (auto t = loadFile (f))
            {
                if (*t == spec)
                    chosen = (int) items.size();
                items.push_back ({ juce::String::fromUTF8 (t->name.c_str()), *t, f, {} });
            }
        const auto count = items.size();
        presetGrid.setItems (std::move (items));
        presetGrid.setChosen (chosen);
        if (count != shownPresets)
        {
            shownPresets = count;
            resized(); // (the grid's height follows the list)
        }

        imageName.setText (spec.image.empty() ? tr ("No image") : imageFile (spec.image).getFileName(), juce::dontSendNotification);
        // the Backgrounds folder's images
        backgroundFiles = backgroundsFolder().findChildFiles (juce::File::findFiles, false, "*.png;*.jpg;*.jpeg;*.gif");
        backgroundFiles.sort();
        imageList.clear (juce::dontSendNotification);
        for (int i = 0; i < backgroundFiles.size(); ++i)
        {
            imageList.addItem (backgroundFiles[i].getFileName(), i + 1);
            if (! spec.image.empty() && backgroundFiles[i] == imageFile (spec.image))
                imageList.setSelectedId (i + 1, juce::dontSendNotification);
        }
        imageList.setTextWhenNothingSelected (backgroundFiles.isEmpty() ? tr ("(no images in the folder yet)") : tr ("Pick an image..."));
        imageList.setEnabled (! backgroundFiles.isEmpty());
        soundName.setText (spec.startupSound.empty() ? tr ("LYNX SHARE's own sound") : juce::File (juce::String::fromUTF8 (spec.startupSound.c_str())).getFileName(),
                           juce::dontSendNotification);
        soundDefault.setEnabled (! spec.startupSound.empty());
        removeImage.setEnabled (! spec.image.empty());
        fitBox.setSelectedId ((int) spec.fit + 1, juce::dontSendNotification);
        opacitySlider.setValue (std::round (spec.imageOpacity * 100.0f), juce::dontSendNotification);
        dimSlider.setValue (std::round (spec.dim * 100.0f), juce::dontSendNotification);
        for (auto* c : std::initializer_list<juce::Component*> { &fitBox, &opacitySlider, &dimSlider })
            c->setEnabled (! spec.image.empty());
        menuStyleBox.setSelectedId (spec.menuStyle == themes::MenuStyle::flClassic ? 2 : 1, juce::dontSendNotification);
        gridSlider.setValue (std::round (spec.gridIntensity * 100.0f), juce::dontSendNotification);
    }

    void choosePreset (int index)
    {
        const auto& items = presetGrid.items();
        if (! juce::isPositiveAndBelow (index, (int) items.size()))
            return;
        const auto chosen = items[(std::size_t) index];
        if (chosen.file == juce::File())
            // a built-in preset sets the colours (and clears the Adjustments and the dots); the workspace image stays
            edit ([&chosen] (themes::Spec& s) {
                const auto image = s.image;
                const auto fit = s.fit;
                const auto opacity = s.imageOpacity, dim = s.dim;
                const auto sound = s.startupSound;
                const auto menuStyle = s.menuStyle;
                const auto grid = s.gridIntensity; // (presets leave the menus and the grid alone, round 190je)
                s = chosen.spec;
                s.menuStyle = menuStyle;
                s.gridIntensity = grid;
                s.image = image;
                s.fit = fit;
                s.imageOpacity = opacity;
                s.dim = dim;
                s.startupSound = sound;
            });
        else if (auto t = loadFile (chosen.file))
            edit ([&t] (themes::Spec& s) { s = *t; });
        if (selectedDot >= 0)
            selectDot (selectedDot);
        else
            select (selected);
    }

    void importTheme()
    {
        chooser = lynx::ui::makeFileChooser (tr ("Import theme"), userThemesFolder(), juce::String ("*") + fileExtension);
        juce::Component::SafePointer<ThemePage> safe (this);
        lynx::ui::launchChooser (chooser, juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe] (const juce::FileChooser& fc) {
            if (safe == nullptr || ! fc.getResult().existsAsFile())
                return;
            if (auto t = loadFile (fc.getResult()))
            {
                safe->edit ([&t] (themes::Spec& s) { s = *t; });
                safe->select (safe->selected);
            }
            else
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Import theme"), tr ("This file is not a LYNX SHARE theme."));
        });
    }

    // the preset's thumbnail (FW-20): the main window at 1/10, once, when the preset is saved
    static void saveThumbnail (const juce::File& presetFile)
    {
        auto* main = mainWindowComponent();
        if (main == nullptr || ! main->isShowing() || main->getWidth() <= 0)
            return;
        auto* top = main->getTopLevelComponent();
        const auto picture = top->createComponentSnapshot (top->getLocalBounds(), true, 0.1f);
        if (! picture.isValid())
            return;
        juce::FileOutputStream out (thumbnailFileFor (presetFile));
        if (out.openedOk())
        {
            out.setPosition (0);
            out.truncate();
            juce::PNGImageFormat().writeImageToStream (picture, out);
        }
    }

    void exportTheme (bool asPreset)
    {
        juce::Component::SafePointer<ThemePage> safe (this);
        if (asPreset)
        {
            auto* window = new juce::AlertWindow (tr ("Save theme preset"), tr ("Name of the preset:"), juce::MessageBoxIconType::NoIcon);
            window->addTextEditor ("name", spec.name == "LYNX" ? tr ("My theme") : juce::String::fromUTF8 (spec.name.c_str()));
            window->addButton (tr ("Save"), 1, juce::KeyPress (juce::KeyPress::returnKey));
            window->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
            window->enterModalState (true, juce::ModalCallbackFunction::create ([safe, window] (int result) {
                                         const auto name = window->getTextEditorContents ("name").trim();
                                         if (result != 1 || name.isEmpty() || safe == nullptr)
                                             return;
                                         safe->spec.name = name.toStdString();
                                         const auto file = userThemesFolder().getChildFile (juce::File::createLegalFileName (name) + fileExtension);
                                         saveFile (safe->spec, file);
                                         safe->flush();
                                         saveThumbnail (file);
                                         safe->refresh();
                                     }),
                                     true);
            keepAboveMainWindow (*window);
            return;
        }
        chooser = lynx::ui::makeFileChooser (tr ("Export theme"),
                                                       userThemesFolder().getChildFile (juce::File::createLegalFileName (juce::String::fromUTF8 (spec.name.c_str())) + fileExtension),
                                                       juce::String ("*") + fileExtension);
        lynx::ui::launchChooser (chooser, juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [safe] (const juce::FileChooser& fc) {
            if (safe == nullptr || fc.getResult() == juce::File())
                return;
            saveFile (safe->spec, fc.getResult().withFileExtension (fileExtension));
            safe->refresh();
        });
    }

    void chooseImage()
    {
        juce::Component::SafePointer<ThemePage> safe (this);
        chooseWorkspaceImage ([safe] (const juce::File& file) {
            if (safe != nullptr)
                safe->edit ([path = imagePathFor (file)] (themes::Spec& s) { s.image = path; });
        });
    }

    void chooseSound()
    {
        chooser = lynx::ui::makeFileChooser (tr ("Startup sound"), juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                       "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        juce::Component::SafePointer<ThemePage> safe (this);
        lynx::ui::launchChooser (chooser, juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe] (const juce::FileChooser& fc) {
            if (safe == nullptr || ! fc.getResult().existsAsFile())
                return;
            // a short sound: it plays in full at every start
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            const std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (fc.getResult()));
            if (reader == nullptr || reader->sampleRate <= 0.0)
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Startup sound"), tr ("LYNX SHARE can't read this audio file."));
                return;
            }
            if ((double) reader->lengthInSamples / reader->sampleRate > 30.0)
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Startup sound"),
                                                        tr ("Choose a sound of 30 seconds or less: it plays in full every time LYNX SHARE starts."));
                return;
            }
            safe->edit ([path = fc.getResult().getFullPathName().toStdString()] (themes::Spec& s) { s.startupSound = path; });
            if (previewStartupSound)
                previewStartupSound (fc.getResult().getFullPathName());
        });
    }

    themes::Spec spec;
    SectionTitle presetTitle { tr ("Presets") }, adjustTitle { tr ("Adjustments") }, coloursTitle { tr ("Colors") }, imageTitle { tr ("Workspace image") };
    PresetGrid presetGrid;
    std::size_t shownPresets = 0;
    juce::TextButton importButton { tr ("Import...") }, exportButton { tr ("Export...") }, savePreset { tr ("Save preset...") },
        resetButton { tr ("Default") }, imageButton { tr ("Choose image...") }, removeImage { tr ("Remove") };
    juce::Label presetNote, adjustNote, contrastNote, imageName, fitLabel, opacityLabel, dimLabel, imageListLabel, imageNote, soundName, soundNote, abNote;
    std::array<juce::Slider, knobCount> knobs;
    std::array<juce::Label, knobCount> knobLabels;
    std::array<DotButton, themes::dotCount> dots { DotButton ({}), DotButton ({}), DotButton ({}), DotButton ({}),
                                                   DotButton ({}), DotButton ({}), DotButton ({}), DotButton ({}) };
    int selectedDot = -1;
    juce::TextButton slotA, slotB;
    std::array<themes::Spec, 2> slots;
    int activeSlot = 0;
    struct Soon final : juce::Timer
    {
        std::function<void()> fire;
        void timerCallback() override
        {
            stopTimer();
            fire();
        }
    } soon;
    bool soonPending = false;
    juce::ComboBox imageList;
    juce::Array<juce::File> backgroundFiles;
    juce::TextButton openBackgrounds { tr ("Open folder") }, soundButton { tr ("Choose sound...") }, soundPlay { tr ("Play") },
        soundDefault { tr ("Default") };
    SectionTitle soundTitle { tr ("Startup sound") }, lookTitle { tr ("Menus and grid") };
    juce::Label menuStyleLabel, gridLabel, lookNote;
    juce::ComboBox menuStyleBox;
    juce::Slider gridSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    ColourWell accentWell { tr ("Accent color") }, backgroundWell { tr ("Background") }, workspaceWell { tr ("Workspace") };
    ColourWell* selected = nullptr;
    juce::ToggleButton autoWorkspace;
    ui::ColourPicker picker;
    juce::ComboBox fitBox;
    juce::Slider opacitySlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight }, dimSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    std::unique_ptr<juce::FileChooser> chooser;
};
} // namespace

void chooseWorkspaceImage (std::function<void (const juce::File&)> done)
{
    static std::unique_ptr<juce::FileChooser> chooser;
    chooser = lynx::ui::makeFileChooser (tr ("Workspace image"), backgroundsFolder(), "*.png;*.jpg;*.jpeg;*.gif");
    lynx::ui::launchChooser (chooser, juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [done] (const juce::FileChooser& fc) {
        if (! fc.getResult().existsAsFile())
            return;
        if (! juce::ImageFileFormat::loadFrom (fc.getResult()).isValid())
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Workspace image"), tr ("LYNX SHARE can't read this image (PNG, JPEG or GIF)."));
            return;
        }
        // kept in the Backgrounds folder (issue #12): a copy, unless it is there already (the same file under that
        // name is reused)
        auto file = fc.getResult();
        const auto folder = backgroundsFolder();
        if (! file.isAChildOf (folder))
        {
            auto copy = folder.getChildFile (file.getFileName());
            if (copy.existsAsFile() && ! copy.hasIdenticalContentTo (file))
                copy = folder.getNonexistentChildFile (file.getFileNameWithoutExtension(), file.getFileExtension(), true);
            if (copy.existsAsFile() || file.copyFileTo (copy))
                file = copy;
        }
        done (file);
    });
}

void setImageWallpaper()
{
    chooseWorkspaceImage ([] (const juce::File& file) {
        auto spec = current();
        spec.image = imagePathFor (file);
        apply (spec);
    });
}

void removeImageWallpaper()
{
    auto spec = current();
    spec.image.clear();
    apply (spec);
}

std::unique_ptr<juce::Component> makeThemePage()
{
    return std::make_unique<ThemePage>();
}
} // namespace lynx::themeui
