#include "ui/PluginThumbs.h"

#include "core/NativeEngines.h"
#include "engine/AudioEngine.h"
#include "platform/Paths.h"
#include "ui/ChannelSettings.h"
#include "ui/ControllerEditors.h"
#include "ui/Controllers164.h"
#include "ui/FacetEqEditor.h"
#include "ui/FlExtraEditors.h"
#include "ui/NewtoneEditor.h"
#include "ui/PatcherEditor.h"
#include "ui/PluginEditors.h"
#include "ui/PluginSkin.h"
#include "ui/ProEditors.h"
#include "ui/QuickPick.h"
#include "ui/VisualEditors.h"

#if JUCE_WINDOWS
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

namespace lynx::pluginthumbs
{
namespace
{
juce::File root()
{
    return platform::cacheFolder ("plugin-thumbs");
}

// a picture written next to its place, then moved in: the picker never reads half a file
bool savePng (const juce::Image& image, const juce::File& file)
{
    if (! image.isValid())
        return false;
    file.getParentDirectory().createDirectory();
    const auto temp = file.getSiblingFile (file.getFileNameWithoutExtension() + ".part");
    temp.deleteFile();
    {
        juce::FileOutputStream out (temp);
        if (! out.openedOk())
            return false;
        juce::PNGImageFormat png;
        if (! png.writeImageToStream (image, out))
            return false;
    }
    return temp.moveFileTo (file);
}

// the song of one plugin and its editor, as the picture shows it (an instrument: its page of the channel window
// without the channel's header)
std::unique_ptr<juce::Component> generatorEditor (AudioEngine& engine, ChannelDef channel)
{
    Song song;
    song.mixer = Song::defaultMixer (2);
    song.channels.push_back (std::move (channel));
    song.patterns.emplace_back();
    engine.setSong (std::move (song));
    engine.dispatchPendingMessages();
    return makeChannelFrame (engine, 0, 0);
}

std::unique_ptr<juce::Component> effectEditor (AudioEngine& engine, EffectSlot slot)
{
    Song song;
    song.mixer = Song::defaultMixer (2);
    const auto kind = slot.kind;
    song.mixer[1].effects.push_back (std::move (slot));
    engine.setSong (std::move (song));
    engine.dispatchPendingMessages();
    if (kind == EffectKind::facetEq)
    {
        auto editor = makeFacetEqEditor (engine, 1, 0, true);
        if (editor != nullptr)
            tickFacetEqEditor (*editor);
        return editor;
    }
    if (ProEditorWindows::handles (kind))
        return makeProEditor (engine, 1, 0, true); // (display-first editors with their demo signal)
    // the effects whose window holds their own editor (as the mixer opens them, PluginEditors.cpp open()), not the
    // generic knob page: else the Patcher, Big Clock, HTML NoteBook, Newtone… were an empty box or a 90 px square
    if (kind == EffectKind::patcher)
        return PatcherWindows::makeSlotEditor (engine, 1, 0);
    if (VisualWindows::handles (kind))
        return makeVisualEditor (engine, 1, 0, true);
    if (FlExtraWindows::handles (kind))
        return makeFlExtraEditor (VisualTarget::mixerSlot (engine, 1, 0), kind, true);
    if (kind == EffectKind::formulaController)
        return FormulaControllerWindows::makeEditor (engine, 1, 0);
    if (NewtoneWindows::handles (kind))
        return makeNewtoneEditor (engine, 1, 0);
    if (Controller164Windows::handles (kind))
    {
        auto locate = [] (Song& song) -> EffectSlot* { return song.mixer.size() > 1 && ! song.mixer[1].effects.empty() ? &song.mixer[1].effects[0] : nullptr; };
        return kind == EffectKind::controlSurface ? makeControlSurfaceEditor (engine, locate, std::nullopt)
                                                  : makeXyControllerEditor (engine, locate, nullptr, std::nullopt);
    }
    return makeNativeEffectEditor (engine, 1, 0, 1.0f, 0);
}

// the editor's drawn part: the frame of the window / channel page around it is often wider than its controls (a
// generator's page is at least the channel window's width), so the borders of the background's own colour are cut,
// keeping a small margin; a very tall editor (Control Surface's column) keeps its top part, at most 1.3 × its width
juce::Image trimToContent (const juce::Image& image)
{
    if (! image.isValid())
        return image;
    const juce::Image::BitmapData data (image, juce::Image::BitmapData::readOnly);
    const auto w = image.getWidth(), h = image.getHeight();
    const auto bg = data.getPixelColour (juce::jmin (3, w - 1), juce::jmin (3, h - 1));
    auto differs = [&] (int x, int y) {
        const auto c = data.getPixelColour (x, y);
        return std::abs ((int) c.getRed() - (int) bg.getRed()) + std::abs ((int) c.getGreen() - (int) bg.getGreen())
                   + std::abs ((int) c.getBlue() - (int) bg.getBlue()) > 24;
    };
    int left = w, right = -1, top = h, bottom = -1;
    for (int y = 0; y < h; y += 2)
        for (int x = 0; x < w; x += 2)
            if (differs (x, y))
            {
                left = juce::jmin (left, x);
                right = juce::jmax (right, x);
                top = juce::jmin (top, y);
                bottom = juce::jmax (bottom, y);
            }
    if (right < left || bottom < top)
        return image;
    auto area = juce::Rectangle<int>::leftTopRightBottom (left, top, right + 2, bottom + 2).expanded (10).getIntersection (image.getBounds());
    if (area.getWidth() < w / 4 || area.getHeight() < h / 6) // (a stray mark: keep it whole)
        area = image.getBounds();
    if (area.getHeight() > area.getWidth() * 13 / 10)
        area.setHeight (area.getWidth() * 13 / 10);
    juce::Image out (juce::Image::ARGB, area.getWidth(), area.getHeight(), true);
    juce::Graphics (out).drawImageAt (image.getClippedImage (area), 0, 0);
    return out;
}

std::unique_ptr<juce::ChildProcess>& child()
{
    static std::unique_ptr<juce::ChildProcess> process;
    return process;
}
bool startedOnce = false;
} // namespace

juce::File nativeFolder()
{
    // (-r2: the pictures trimmed to the editor's drawn part; a new drawing rule = a new folder)
    return root().getChildFile (juce::String (JUCE_APPLICATION_VERSION_STRING).retainCharacters ("0123456789.-abcdefghijklmnopqrstuvwxyz") + "-r2");
}

juce::File capturedFolder()
{
    return root().getChildFile ("plugins");
}

juce::String fileNameFor (const std::string& key)
{
    auto text = juce::String::fromUTF8 (key.c_str());
    for (const auto* prefix : { "lynx:gen:", "lynx:fx:" })
        if (text.startsWith (prefix))
            text = juce::String (juce::String (prefix).contains ("gen") ? "gen-" : "fx-") + text.substring ((int) std::strlen (prefix));
    auto slug = text.toLowerCase().retainCharacters ("abcdefghijklmnopqrstuvwxyz0123456789- ").replaceCharacter (' ', '-').substring (0, 48);
    // (FNV-1a of the whole key: two plugins whose names fold to the same slug still differ)
    std::uint32_t h = 2166136261u;
    for (const auto c : key)
        h = (h ^ (std::uint8_t) c) * 16777619u;
    return slug + "-" + juce::String::toHexString ((juce::int64) h).paddedLeft ('0', 8) + ".png";
}

juce::File fileFor (const std::string& key, bool native)
{
    return (native ? nativeFolder() : capturedFolder()).getChildFile (fileNameFor (key));
}

juce::Image shrink (const juce::Image& image, float pixelsPerUnit)
{
    if (! image.isValid())
        return {};
    const auto scale = juce::jmin ((double) pictureScale / juce::jmax (0.01f, pixelsPerUnit), (double) maxWidth / image.getWidth(),
                                   (double) maxHeight / image.getHeight());
    if (scale >= 1.0)
        return image.convertedToFormat (juce::Image::ARGB);
    const auto w = juce::jmax (1, juce::roundToInt (image.getWidth() * scale));
    const auto h = juce::jmax (1, juce::roundToInt (image.getHeight() * scale));
    // halve first while far above the target (a box filter keeps the fine lines), then the last step
    auto from = image.convertedToFormat (juce::Image::ARGB);
    while (from.getWidth() >= w * 2 && from.getHeight() >= h * 2)
        from = from.rescaled (from.getWidth() / 2, from.getHeight() / 2, juce::Graphics::highResamplingQuality);
    return from.rescaled (w, h, juce::Graphics::highResamplingQuality);
}

int renderNative (const juce::File& folder, const juce::StringArray& only, const std::function<void (const juce::String&)>& progress)
{
    folder.createDirectory();
    // one drawing process at a time for this folder (two LYNX windows open the picker together)
    juce::InterProcessLock lock ("LYNX-plugin-thumbs-" + juce::String::toHexString (folder.getFullPathName().hashCode()));
    if (! lock.enter (0))
        return 0;

    skin::setArtMotion (false); // (a still picture: the moving layers stay at their first frame)
    AudioEngine engine (false);
    int drawn = 0;
    auto shoot = [&] (const std::string& key, std::unique_ptr<juce::Component> editor, int cropTop) {
        const auto file = folder.getChildFile (fileNameFor (key));
        if (editor == nullptr || editor->getWidth() <= 0 || editor->getHeight() <= cropTop)
        {
            if (progress)
                progress (juce::String::fromUTF8 (key.c_str()) + ": no editor");
            return;
        }
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        const auto area = editor->getLocalBounds().withTrimmedTop (cropTop);
        const auto picture = shrink (trimToContent (editor->createComponentSnapshot (area, true, 1.0f)));
        if (savePng (picture, file))
            ++drawn;
        if (progress)
            progress (file.getFileName() + " " + juce::String (picture.getWidth()) + "x" + juce::String (picture.getHeight()) + " "
                      + juce::String (juce::Time::getMillisecondCounterHiRes() - t0, 0) + " ms");
    };
    auto wanted = [&] (const std::string& key) {
        if (! only.isEmpty() && ! only.contains (juce::String::fromUTF8 (key.c_str())))
            return false;
        return ! folder.getChildFile (fileNameFor (key)).existsAsFile();
    };

    for (const auto& g : quickpick::generatorEntries (engine))
    {
        if (g.folder == 3 || ! g.available || ! g.make || ! wanted (g.key))
            continue;
        auto channel = g.make();
        if (! channel.has_value())
            continue;
        if (channel->instrument == InstrumentKind::patcher) // (its window is its node graph, as the Channel Rack opens it)
        {
            auto frame = generatorEditor (engine, std::move (*channel));
            shoot (g.key, PatcherWindows::makeChannelEditor (engine, 0), 0);
            continue;
        }
        shoot (g.key, generatorEditor (engine, std::move (*channel)), channelFrameHeaderHeight());
    }
    for (const auto& f : quickpick::effectEntries (engine))
    {
        if (f.folder == 3 || ! f.make || ! wanted (f.key))
            continue;
        auto slot = f.make();
        if (! slot.has_value())
            continue;
        shoot (f.key, effectEditor (engine, std::move (*slot)), 0);
    }
    engine.setSong (Song {});
    return drawn;
}

bool rendering()
{
    auto& p = child();
    return p != nullptr && p->isRunning();
}

bool ensureRendering (bool again)
{
    if (rendering())
        return true;
    if (startedOnce && ! again)
        return false;
    startedOnce = true;
    auto& p = child();
    p = std::make_unique<juce::ChildProcess>();
    const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    if (! p->start (juce::StringArray { exe.getFullPathName(), "--pluginthumbs", nativeFolder().getFullPathName() }, 0))
    {
        p.reset();
        return false;
    }
    return true;
}

void captureEditor (const std::string& key, juce::Component& editor)
{
   #if JUCE_WINDOWS
    const auto file = fileFor (key, false);
    if (key.empty() || file.existsAsFile() || ! editor.isShowing())
        return;
    auto* peer = editor.getPeer();
    if (peer == nullptr)
        return;
    auto* hwnd = (HWND) peer->getNativeHandle();
    RECT r {};
    if (hwnd == nullptr || ! GetClientRect (hwnd, &r) || r.right <= 8 || r.bottom <= 8)
        return;
    const auto w = (int) r.right, h = (int) r.bottom;
    auto* screen = GetDC (nullptr);
    auto* dc = CreateCompatibleDC (screen);
    BITMAPINFO info {};
    info.bmiHeader.biSize = sizeof (BITMAPINFOHEADER);
    info.bmiHeader.biWidth = w;
    info.bmiHeader.biHeight = -h; // top-down
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    auto* bitmap = CreateDIBSection (dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    juce::Image picture;
    if (bitmap != nullptr && bits != nullptr)
    {
        auto* old = SelectObject (dc, bitmap);
        // PW_CLIENTONLY | PW_RENDERFULLCONTENT: the client area with the plugin's own (GPU) drawing in it
        if (PrintWindow (hwnd, dc, 0x1 | 0x2))
        {
            GdiFlush();
            picture = juce::Image (juce::Image::RGB, w, h, false);
            juce::Image::BitmapData out (picture, juce::Image::BitmapData::writeOnly);
            for (int y = 0; y < h; ++y)
            {
                const auto* row = static_cast<const std::uint8_t*> (bits) + (std::size_t) y * (std::size_t) w * 4;
                for (int x = 0; x < w; ++x)
                    out.setPixelColour (x, y, juce::Colour (row[x * 4 + 2], row[x * 4 + 1], row[x * 4 + 0]));
            }
        }
        SelectObject (dc, old);
        DeleteObject (bitmap);
    }
    DeleteDC (dc);
    ReleaseDC (nullptr, screen);

    // the editor's part only (not the window's own LYNX bar): its area in the window, in physical pixels
    const auto scale = (float) w / (float) juce::jmax (1, peer->getComponent().getWidth());
    if (picture.isValid())
    {
        const auto& top = peer->getComponent();
        const auto area = (top.getLocalArea (&editor, editor.getLocalBounds()).toFloat() * scale).toNearestInt().getIntersection ({ 0, 0, w, h });
        if (area.getWidth() < 16 || area.getHeight() < 16)
            return;
        picture = picture.getClippedImage (area);
    }
    if (picture.isValid())
    {
        const auto pw = picture.getWidth(), ph = picture.getHeight();
        // a fully black or white capture = the plugin drew nothing PrintWindow can see: keep the tile instead
        juce::Image::BitmapData data (picture, juce::Image::BitmapData::readOnly);
        const auto first = data.getPixelColour (pw / 2, ph / 2);
        bool flat = true;
        for (int y = 0; y < ph && flat; y += juce::jmax (1, ph / 16))
            for (int x = 0; x < pw && flat; x += juce::jmax (1, pw / 16))
                flat = data.getPixelColour (x, y) == first;
        if (! flat)
        {
            // (a clipped image shares its pixels: copy it out so the PNG writer and the scaler see only the editor)
            juce::Image own (juce::Image::RGB, pw, ph, false);
            juce::Graphics (own).drawImageAt (picture, 0, 0);
            savePng (shrink (trimToContent (own), scale), file);
        }
    }
   #else
    juce::ignoreUnused (key, editor);
   #endif
}
} // namespace lynx::pluginthumbs
