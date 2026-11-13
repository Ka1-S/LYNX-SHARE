#include "ui/WindowOwner.h"
#include "core/HostKeyGate.h"
#include "ui/NativeChild.h"

#include "core/WindowFit.h"

#include "ui/LogoData.h"
#include "ui/Theme.h"
#include "ui/ImageCaches.h"
#include "platform/Paths.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <ctime>

#if JUCE_WINDOWS
 #include <windows.h>
#endif
#if JUCE_LINUX || JUCE_BSD || JUCE_MAC
 #include <dlfcn.h>
#endif
#if JUCE_MAC
 #include <CoreFoundation/CoreFoundation.h>
#endif

namespace lynx
{
namespace
{
juce::Component::SafePointer<juce::Component>& mainWindow()
{
    static juce::Component::SafePointer<juce::Component> window;
    return window;
}
} // namespace

void* mainWindowHandle()
{
    auto* main = mainWindow().getComponent();
    return main != nullptr && main->getPeer() != nullptr ? main->getWindowHandle() : nullptr;
}

namespace
{
// Every time the main window comes to the front (a click in it, the drag image hiding at a drop...), JUCE moves it to
// the top of its list; the windows kept above it go back above it there (matchWindowOrderToScreen, issue #12)
struct OrderKeeper final : juce::ComponentListener
{
    void componentBroughtToFront (juce::Component&) override { matchWindowOrderToScreen(); }
};

OrderKeeper& orderKeeper()
{
    static OrderKeeper keeper;
    return keeper;
}
} // namespace

void setMainWindowForOwnership (juce::Component* window)
{
    if (auto* before = mainWindow().getComponent())
        before->removeComponentListener (&orderKeeper());
    mainWindow() = window;
    if (window != nullptr)
        window->addComponentListener (&orderKeeper());
}

void activateMainWindow()
{
    auto* main = mainWindow().getComponent();
    if (main == nullptr || ! main->isShowing())
        return;
    auto* top = main->getTopLevelComponent();
    if (auto* peer = top->getPeer(); peer != nullptr && ! peer->isMinimised())
        top->toFront (true);
}

double processCpuSeconds()
{
   #if JUCE_WINDOWS
    FILETIME created {}, exited {}, kernel {}, user {};
    if (GetProcessTimes (GetCurrentProcess(), &created, &exited, &kernel, &user))
        return (double) ((((std::uint64_t) kernel.dwHighDateTime << 32) | kernel.dwLowDateTime) + (((std::uint64_t) user.dwHighDateTime << 32) | user.dwLowDateTime)) * 1e-7;
    return 0.0;
   #else
    return (double) std::clock() / CLOCKS_PER_SEC;
   #endif
}

double threadCpuSeconds()
{
   #if JUCE_WINDOWS
    FILETIME created {}, exited {}, kernel {}, user {};
    if (GetThreadTimes (GetCurrentThread(), &created, &exited, &kernel, &user))
        return (double) ((((std::uint64_t) kernel.dwHighDateTime << 32) | kernel.dwLowDateTime) + (((std::uint64_t) user.dwHighDateTime << 32) | user.dwLowDateTime)) * 1e-7;
    return 0.0;
   #elif defined (CLOCK_THREAD_CPUTIME_ID)
    timespec t {};
    return clock_gettime (CLOCK_THREAD_CPUTIME_ID, &t) == 0 ? (double) t.tv_sec + (double) t.tv_nsec * 1e-9 : 0.0;
   #else
    return 0.0;
   #endif
}

juce::TextEditor* firstTextField (juce::Component& dialog)
{
    juce::TextEditor* best = nullptr;
    juce::Point<int> bestAt;
    std::function<void (juce::Component&)> walk = [&] (juce::Component& c) {
        for (auto* child : c.getChildren())
        {
            if (! child->isVisible() || ! child->isEnabled())
                continue;
            if (auto* editor = dynamic_cast<juce::TextEditor*> (child); editor != nullptr && ! editor->isReadOnly() && editor->getWantsKeyboardFocus())
            {
                const auto at = dialog.getLocalPoint (editor, juce::Point<int>());
                if (best == nullptr || at.y < bestAt.y || (at.y == bestAt.y && at.x < bestAt.x))
                {
                    best = editor;
                    bestAt = at;
                }
                continue;
            }
            walk (*child);
        }
    };
    walk (dialog);
    return best;
}

namespace
{
struct DialogTextFocus final : juce::FocusChangeListener
{
    void globalFocusChanged (juce::Component* focused) override
    {
        if (focused == nullptr)
            return;
        auto* dialog = dynamic_cast<juce::AlertWindow*> (focused->getTopLevelComponent());
        if (dialog == nullptr || ! dialog->isShowing())
            return;
        auto& props = dialog->getProperties();
        static const juce::Identifier done ("lynxTextFocused");
        if (props.contains (done)) // only the dialog's first focus: Tab and clicks move it freely afterwards
            return;
        props.set (done, true);
        if (dynamic_cast<juce::TextEditor*> (focused) != nullptr)
            return;
        if (auto* field = firstTextField (*dialog))
            field->grabKeyboardFocus(); // (AlertWindow's fields select all their text when focused)
    }
};

std::unique_ptr<DialogTextFocus>& dialogTextFocus()
{
    static std::unique_ptr<DialogTextFocus> listener;
    return listener;
}
} // namespace

void installDialogTextFocus()
{
    if (dialogTextFocus() != nullptr)
        return;
    dialogTextFocus() = std::make_unique<DialogTextFocus>();
    juce::Desktop::getInstance().addFocusChangeListener (dialogTextFocus().get());
}

void removeDialogTextFocus()
{
    if (dialogTextFocus() == nullptr)
        return;
    juce::Desktop::getInstance().removeFocusChangeListener (dialogTextFocus().get());
    dialogTextFocus() = nullptr;
}

juce::Image logoImage()
{
    return juce::ImageCache::getFromMemory (logo::png512, (int) sizeof (logo::png512));
}

juce::Image logoSmallImage()
{
    return juce::ImageCache::getFromMemory (logo::pngSmall, (int) sizeof (logo::pngSmall));
}

juce::Image logoTinyImage()
{
    return juce::ImageCache::getFromMemory (logo::pngTiny, (int) sizeof (logo::pngTiny));
}

juce::Image logoMarkImage()
{
    return juce::ImageCache::getFromMemory (logo::markPng512, (int) sizeof (logo::markPng512));
}

juce::Image logoMarkSmallImage()
{
    return juce::ImageCache::getFromMemory (logo::markSmall, (int) sizeof (logo::markSmall));
}

LogoDisc logoDisc()
{
    return { logo::discX, logo::discY, logo::discR };
}

juce::Image logoAt (int px)
{
    px = juce::jlimit (8, 512, px);
    static std::map<int, juce::Image> cache;
    static const bool registered = lynx::ui::registerImageCache ([] { cache.clear(); }); (void) registered;
    auto& img = cache[px];
    if (img.isNull())
    {
        img = px <= 20 ? logoTinyImage() : px <= 32 ? logoSmallImage() : logoImage();
        while (img.getWidth() / 2 >= px)
            img = img.rescaled (img.getWidth() / 2, img.getHeight() / 2, juce::Graphics::highResamplingQuality);
        if (img.getWidth() != px)
            img = img.rescaled (px, px, juce::Graphics::highResamplingQuality);
    }
    return img;
}

void applyWindowChrome (juce::Component& window)
{
   #if JUCE_WINDOWS
    // dark native title bar (Windows 10 1809+), tinted with the LYNX background on Windows 11
    auto* hwnd = (HWND) window.getWindowHandle();
    if (hwnd == nullptr)
        return;

    using SetAttribute = HRESULT (WINAPI*) (HWND, DWORD, LPCVOID, DWORD);
    static const auto setAttribute = [] {
        auto* dwm = LoadLibraryW (L"dwmapi.dll");
        return dwm != nullptr ? (SetAttribute) (void*) GetProcAddress (dwm, "DwmSetWindowAttribute") : nullptr;
    }();

    if (setAttribute == nullptr)
        return;

    const BOOL dark = TRUE;
    if (FAILED (setAttribute (hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof (dark))))
        setAttribute (hwnd, 19 /* same attribute before Windows 10 20H1 */, &dark, sizeof (dark));

    auto colorRef = [] (juce::Colour c) { return (DWORD) (c.getRed() | (c.getGreen() << 8) | (c.getBlue() << 16)); };
    const auto caption = colorRef (theme::colour::bg1);
    const auto text = colorRef (theme::colour::textPrimary);
    const auto border = colorRef (theme::colour::border);
    setAttribute (hwnd, 35 /* DWMWA_CAPTION_COLOR */, &caption, sizeof (caption));
    setAttribute (hwnd, 36 /* DWMWA_TEXT_COLOR */, &text, sizeof (text));
    setAttribute (hwnd, 34 /* DWMWA_BORDER_COLOR */, &border, sizeof (border));
   #else
    juce::ignoreUnused (window);
   #endif
}

namespace
{
void applyOwnership (juce::Component& window)
{
    applyWindowChrome (window);

    if (auto* top = dynamic_cast<juce::TopLevelWindow*> (&window))
        if (auto* peer = top->getPeer())
            peer->setIcon (logoImage());
   #if JUCE_WINDOWS
    auto* owner = mainWindow().getComponent();
    if (owner == nullptr || owner == &window || window.getWindowHandle() == nullptr || owner->getWindowHandle() == nullptr)
        return;
    // "owned" window: Windows keeps it above its owner and minimizes it along with it
    SetWindowLongPtrW ((HWND) window.getWindowHandle(), GWLP_HWNDPARENT, (LONG_PTR) owner->getWindowHandle());
   #endif
}

// JUCE recreates a window's native peer when its style changes (setResizable, setUsingNativeTitleBar...
// after it is on screen). The new native window has no owner, so it fell behind the main window
// (issue #4: the Audio editor opened behind LYNX). This re-applies the ownership to every new peer.
class OwnershipKeeper final : public juce::ComponentListener
{
public:
    explicit OwnershipKeeper (juce::Component& w) : window (&w) { w.addComponentListener (this); }
    ~OwnershipKeeper() override
    {
        if (window != nullptr)
            window->removeComponentListener (this);
    }

    void componentParentHierarchyChanged (juce::Component& c) override
    {
        if (c.getPeer() != nullptr) // (also called while the old peer is still there: harmless)
            applyOwnership (c);
    }

    // round 170h: an owned window that goes away while it is the active one (closed, hidden, deleted) gives the
    // activation back to the main window, not to whatever program Windows picks next (the shortcuts and Space did
    // nothing after closing a dialog until a click in LYNX). Windows that do it themselves first are not affected.
    void componentVisibilityChanged (juce::Component& c) override
    {
        if (! c.isVisible())
            if (auto* top = dynamic_cast<juce::TopLevelWindow*> (&c); top != nullptr && top->isActiveWindow())
                activateMainWindow();
    }

    void componentBeingDeleted (juce::Component& c) override;

    juce::Component* window;
};

std::map<juce::Component*, std::unique_ptr<OwnershipKeeper>>& keepers()
{
    static std::map<juce::Component*, std::unique_ptr<OwnershipKeeper>> all;
    return all;
}

void OwnershipKeeper::componentBeingDeleted (juce::Component& c)
{
   #if JUCE_WINDOWS
    // (deleted while still shown and active: hand the activation over before its native window goes)
    if (c.isVisible() && c.getPeer() != nullptr && GetForegroundWindow() == (HWND) c.getWindowHandle())
        activateMainWindow();
   #endif
    c.removeComponentListener (this);
    window = nullptr;
    keepers().erase (&c); // deletes this
}
} // namespace

bool isOwnedByMainWindow (juce::Component& window)
{
   #if JUCE_WINDOWS
    auto* owner = mainWindow().getComponent();
    return owner != nullptr && window.getWindowHandle() != nullptr && owner->getWindowHandle() != nullptr
           && GetWindow ((HWND) window.getWindowHandle(), GW_OWNER) == (HWND) owner->getWindowHandle();
   #else
    return keepers().count (&window) > 0;
   #endif
}

bool isAboveMainWindow (juce::Component& window)
{
   #if JUCE_WINDOWS
    auto* owner = mainWindow().getComponent();
    if (owner == nullptr || window.getWindowHandle() == nullptr || owner->getWindowHandle() == nullptr)
        return false;
    for (auto h = GetWindow ((HWND) window.getWindowHandle(), GW_HWNDNEXT); h != nullptr; h = GetWindow (h, GW_HWNDNEXT))
        if (h == (HWND) owner->getWindowHandle())
            return true;
    return false;
   #else
    return isOwnedByMainWindow (window);
   #endif
}

// Where a character key is on the keyboard, for the typing keyboard (FL's table goes by scan code)
#if JUCE_WINDOWS
namespace
{
// The scan code of the key that types `ch` in `layout` without Shift / AltGr; 0 for none
int scanCodeOfCharacter (int ch, HKL layout)
{
    for (const auto c : { (WCHAR) towlower ((wint_t) ch), (WCHAR) ch })
    {
        const auto vk = VkKeyScanExW (c, layout);
        if (vk == -1 || HIBYTE (vk) != 0) // (no such key, or only with Shift / Ctrl / Alt held)
            continue;
        const auto scanCode = (int) MapVirtualKeyExW ((UINT) LOBYTE (vk), MAPVK_VK_TO_VSC, layout);
        return scanCode > 0 && scanCode < 0x80 ? scanCode : 0;
    }
    return 0;
}
} // namespace

int scanCodeOfKeyCode (int juceKeyCode)
{
    // JUCE's code is MapVirtualKey (vk, MAPVK_VK_TO_CHAR) of the pressed key (upper case for letters). Back to the
    // key: the layout's virtual key for that character, then that key's scan code.
    if (juceKeyCode <= ' ' || juceKeyCode > 0xffff)
        return 0; // (Space, F-keys, arrows, the numpad: not character keys)
    return scanCodeOfCharacter (juceKeyCode, GetKeyboardLayout (0));
}
#elif JUCE_LINUX || JUCE_BSD
namespace
{
// Xlib, looked up at run time (JUCE loads it the same way and keeps its own headers to itself). A connection of its
// own, used for keyboard questions only and renewed every few seconds, so a keyboard map changed meanwhile is seen.
struct XKeyboard
{
    using OpenDisplay = void* (*) (const char*);
    using CloseDisplay = int (*) (void*);
    using DisplayKeycodes = int (*) (void*, int*, int*);
    using GetState = int (*) (void*, unsigned int, void*);
    using KeycodeToKeysym = unsigned long (*) (void*, unsigned char, int, int);

    XKeyboard()
    {
        if (auto* xlib = dlopen ("libX11.so.6", RTLD_LAZY | RTLD_LOCAL))
        {
            openDisplay = (OpenDisplay) dlsym (xlib, "XOpenDisplay");
            closeDisplay = (CloseDisplay) dlsym (xlib, "XCloseDisplay");
            displayKeycodes = (DisplayKeycodes) dlsym (xlib, "XDisplayKeycodes");
            getState = (GetState) dlsym (xlib, "XkbGetState");
            keycodeToKeysym = (KeycodeToKeysym) dlsym (xlib, "XkbKeycodeToKeysym");
        }
    }
    ~XKeyboard() { close(); }

    void close()
    {
        if (display != nullptr)
            closeDisplay (display);
        display = nullptr;
    }

    // The scan code (evdev's key code = Xlib's - 8; the typing block's are the PC scan codes) of the key whose
    // unshifted keysym in the layout in use (the active XKB group) is `keysym` or `otherKeysym`; 0 for none
    int scanCodeOf (unsigned long keysym, unsigned long otherKeysym)
    {
        if (openDisplay == nullptr || closeDisplay == nullptr || displayKeycodes == nullptr || getState == nullptr || keycodeToKeysym == nullptr)
            return 0;
        const auto now = std::time (nullptr);
        if (display != nullptr && now - opened > 2)
            close();
        if (display == nullptr)
        {
            display = openDisplay (nullptr);
            opened = now;
        }
        if (display == nullptr)
            return 0;
        alignas (16) unsigned char state[64] = {}; // XkbStateRec (18 bytes); its first field is the active group
        const auto group = getState (display, 0x100 /* XkbUseCoreKbd */, state) == 0 ? (int) state[0] : 0;
        int first = 8, last = 255;
        displayKeycodes (display, &first, &last);
        for (int keycode = std::max (first, 9); keycode <= std::min (last, 8 + 0x7f); ++keycode)
            if (const auto sym = keycodeToKeysym (display, (unsigned char) keycode, group, 0); sym != 0 && (sym == keysym || sym == otherKeysym))
                return keycode - 8;
        return 0;
    }

    OpenDisplay openDisplay = nullptr;
    CloseDisplay closeDisplay = nullptr;
    DisplayKeycodes displayKeycodes = nullptr;
    GetState getState = nullptr;
    KeycodeToKeysym keycodeToKeysym = nullptr;
    void* display = nullptr;
    std::time_t opened = 0;
};
} // namespace

int scanCodeOfKeyCode (int juceKeyCode)
{
    // JUCE's code is the character the key types. Back to the key: the key whose keysym is that character (Latin-1
    // keysyms are the characters; the others' are 0x01000000 + the code point, which leaves out alphabets that have
    // older keysyms of their own: those keys stay by character)
    if (juceKeyCode <= ' ' || juceKeyCode > 0x10ffff)
        return 0;
    static XKeyboard keyboard;
    const auto keysymOf = [] (int ch) { return (unsigned long) (ch < 0x100 ? ch : 0x01000000 + ch); };
    return keyboard.scanCodeOf (keysymOf ((int) juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) juceKeyCode)), keysymOf (juceKeyCode));
}
#elif JUCE_MAC
int scanCodeOfKeyCode (int juceKeyCode)
{
    // JUCE's code is the key's character without modifiers. Back to the key: the virtual key code (a place on the
    // keyboard: kVK_ANSI_*) whose character in the current layout is that one, asked from the Text Input Sources /
    // UCKeyTranslate. Those live in Carbon's HIToolbox, which LYNX does not link: looked up at run time, and when
    // anything is missing the answer is 0 (the typing keyboard then goes by character, as before).
    if (juceKeyCode <= ' ' || juceKeyCode > 0xffff)
        return 0;
    struct Api
    {
        using CopyLayout = void* (*)();
        using GetProperty = void* (*) (void*, CFStringRef);
        using KbdType = std::uint8_t (*)();
        using Translate = std::int32_t (*) (const void*, std::uint16_t, std::uint16_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t*,
                                            unsigned long, unsigned long*, std::uint16_t*);
        CopyLayout copyLayout = nullptr;
        GetProperty getProperty = nullptr;
        KbdType kbdType = nullptr;
        Translate translate = nullptr;
        CFStringRef* layoutDataKey = nullptr;
        Api()
        {
            if (auto* carbon = dlopen ("/System/Library/Frameworks/Carbon.framework/Carbon", RTLD_LAZY | RTLD_LOCAL))
            {
                copyLayout = (CopyLayout) dlsym (carbon, "TISCopyCurrentKeyboardLayoutInputSource");
                getProperty = (GetProperty) dlsym (carbon, "TISGetInputSourceProperty");
                kbdType = (KbdType) dlsym (carbon, "LMGetKbdType");
                translate = (Translate) dlsym (carbon, "UCKeyTranslate");
                layoutDataKey = (CFStringRef*) dlsym (carbon, "kTISPropertyUnicodeKeyLayoutData");
            }
        }
        bool ok() const { return copyLayout != nullptr && getProperty != nullptr && kbdType != nullptr && translate != nullptr && layoutDataKey != nullptr; }
    };
    static const Api api;
    if (! api.ok())
        return 0;
    // kVK_ANSI_* (0x00..0x32) -> PC scan code; 0: not a key of the typing block
    static const std::uint8_t scanOfVirtualKey[] = {
        0x1E, 0x1F, 0x20, 0x21, 0x23, 0x22, 0x2C, 0x2D, 0x2E, 0x2F, 0x00, 0x30, 0x10, 0x11, 0x12, 0x13, // A S D F H G Z X C V - B Q W E R
        0x15, 0x14, 0x02, 0x03, 0x04, 0x05, 0x07, 0x06, 0x0D, 0x0A, 0x08, 0x0C, 0x09, 0x0B, 0x1B, 0x18, // Y T 1 2 3 4 6 5 = 9 7 - 8 0 ] O
        0x16, 0x1A, 0x17, 0x19, 0x00, 0x26, 0x24, 0x28, 0x25, 0x27, 0x2B, 0x33, 0x35, 0x31, 0x32, 0x34, // U [ I P Ret L J ' K ; \ , / N M .
        0x00, 0x00, 0x29                                                                                // Tab Space `
    };
    auto* source = api.copyLayout();
    if (source == nullptr)
        return 0;
    int found = 0;
    if (const auto data = (CFDataRef) api.getProperty (source, *api.layoutDataKey); data != nullptr)
    {
        const auto wanted = juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) juceKeyCode);
        for (std::uint16_t vk = 0; vk < (std::uint16_t) std::size (scanOfVirtualKey) && found == 0; ++vk)
        {
            std::uint32_t deadKeys = 0;
            unsigned long length = 0;
            std::uint16_t chars[4] = {};
            if (scanOfVirtualKey[vk] != 0
                && api.translate (CFDataGetBytePtr (data), vk, 0 /* kUCKeyActionDown */, 0, api.kbdType(), 1 /* kUCKeyTranslateNoDeadKeysMask */, &deadKeys,
                                  4, &length, chars) == 0
                && length == 1 && juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) chars[0]) == wanted)
                found = scanOfVirtualKey[vk];
        }
    }
    CFRelease ((CFTypeRef) source);
    return found;
}
#else
int scanCodeOfKeyCode (int)
{
    return 0;
}
#endif

void forceForegroundForTest (juce::Component& window)
{
   #if JUCE_WINDOWS
    // a process started in the background may not take the foreground: join the input of the foreground window's
    // thread for a moment (what UI automation tools do)
    auto* hwnd = (HWND) window.getWindowHandle();
    auto* foreground = GetForegroundWindow();
    if (hwnd == nullptr || foreground == hwnd)
        return;
    const auto theirs = GetWindowThreadProcessId (foreground, nullptr), ours = GetCurrentThreadId();
    AttachThreadInput (theirs, ours, TRUE);
    SetForegroundWindow (hwnd);
    SetFocus (hwnd);
    AttachThreadInput (theirs, ours, FALSE);
   #else
    window.toFront (true);
   #endif
}

namespace
{
int gpuSetting = -1; // −1 not read yet

void applyToPeer (juce::ComponentPeer& peer, bool gpu)
{
   #if JUCE_WINDOWS
    const auto engines = peer.getAvailableRenderingEngines();
    if (engines.size() < 2)
        return;
    const auto wanted = gpu ? engines.size() - 1 : 0; // JUCE 8: 0 = GDI ("Software Renderer"), last = Direct2D
    if (peer.getCurrentRenderingEngine() != wanted)
        peer.setCurrentRenderingEngine (wanted);
   #else
    (void) peer;
    (void) gpu;
   #endif
}

struct RenderingWatch final : juce::Timer
{
    void timerCallback() override
    {
        if (gpuRenderingEnabled())
            return; // (JUCE's default: nothing to change)
        for (int i = juce::ComponentPeer::getNumPeers(); --i >= 0;)
            if (auto* peer = juce::ComponentPeer::getPeer (i))
                applyToPeer (*peer, false);
    }
};
} // namespace

bool gpuRenderingEnabled()
{
    if (gpuSetting < 0)
        gpuSetting = platform::settingsFile ("gpu-rendering.txt").loadFileAsString().trim() == "0" ? 0 : 1;
    return gpuSetting != 0;
}

void setGpuRenderingEnabled (bool on)
{
    gpuSetting = on ? 1 : 0;
    platform::settingsFile ("gpu-rendering.txt").replaceWithText (on ? "1" : "0");
    for (int i = juce::ComponentPeer::getNumPeers(); --i >= 0;)
        if (auto* peer = juce::ComponentPeer::getPeer (i))
        {
            applyToPeer (*peer, on && ! peer->getComponent().getProperties()["lynxGdi"]); // (plugin windows stay on GDI)
            peer->getComponent().repaint();
        }
}

void startRenderingWatch()
{
   #if JUCE_WINDOWS
    static RenderingWatch watch;
    watch.startTimer (500);
   #endif
}

void applyRenderingChoice (juce::Component& window)
{
    if (! gpuRenderingEnabled())
        if (auto* peer = window.getPeer())
            applyToPeer (*peer, false);
}

namespace
{
bool closeOnEscape (juce::Component* window);

std::function<bool (juce::Component&)> plainKeysAllowed;
std::function<bool (const juce::KeyPress&)> plainKeyHandler, plainKeyLeftHandler;

// The keys a hosted plugin's view leaves unused come back through JUCE's hosting components, which swallow them all
// after their key listeners ran
struct PluginLeftKeys final : juce::KeyListener
{
    bool keyPressed (const juce::KeyPress& key, juce::Component*) override
    {
        const auto mods = key.getModifiers();
        return plainKeyLeftHandler != nullptr && key.getKeyCode() != juce::KeyPress::escapeKey && ! mods.isCtrlDown() && ! mods.isAltDown()
               && plainKeyLeftHandler (key);
    }
};

struct SecondaryKeyForwarder final : juce::KeyListener
{
    std::function<bool (const juce::KeyPress&)> handler;

    bool keyPressed (const juce::KeyPress& key, juce::Component* window) override
    {
        if (window != nullptr && (bool) window->getProperties().getWithDefault ("lynxOwnKeys", false))
            return false;
        if (key == juce::KeyPress::escapeKey)
            return closeOnEscape (window);
        // a plain key a plugin's window got back: its view left it unused (issue #96). Plugins pass them up to their parent
        // window, which is this one, so nothing inside the window saw them.
        const auto mods = key.getModifiers();
        if (window != nullptr && (bool) window->getProperties().getWithDefault ("lynxPluginWindow", false) && plainKeyLeftHandler != nullptr
            && ! mods.isCtrlDown() && ! mods.isAltDown() && plainKeyLeftHandler (key))
            return true;
        if (handler == nullptr)
            return false;
        return handler (key);
    }
};

// FL closes the focused plugin / channel / tool window with Esc (round 170h, FL 2026 driven: a 808 HiHat channel
// window, Sawer, Fruity Parametric EQ 2). LYNX's own editors that use Esc (a text field, a selection) take it first;
// only an Esc nothing in the window used closes it. Async: the window may delete itself in closeButtonPressed.
bool closeOnEscape (juce::Component* window)
{
    auto* doc = dynamic_cast<juce::DocumentWindow*> (window);
    if (doc == nullptr || ! doc->isVisible())
        return false;
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<juce::DocumentWindow> (doc)] {
        if (auto* w = safe.getComponent(); w != nullptr && w->isVisible())
            w->closeButtonPressed();
    });
    return true;
}

SecondaryKeyForwarder& keyForwarder()
{
    static SecondaryKeyForwarder forwarder;
    return forwarder;
}
} // namespace

#if JUCE_WINDOWS
// A hosted plugin's own view (VST3 / VST2 / CLAP) gets the keys itself: with the focus in its native child window the
// keys never reach JUCE, and with the focus on JUCE's wrapper the wrapper swallows them all. FL still runs its
// shortcuts there (Space plays), so a thread keyboard hook hands such a key to the main window's global actions when
// it is one, before the plugin sees it (round 165q / issue #12: Space did nothing over Pro-Q 4). Plain letters and
// digits stay the plugin's (its own keys and text fields) unless its FL-style option lets the host process them
// (round 181c, issue #96: the typing keyboard over a focused plugin), and everything typed into a native Edit control
// stays the plugin's either way.
namespace
{
int juceKeyCodeOf (WPARAM vk)
{
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9'))
        return (int) vk;
    if (vk >= VK_F1 && vk <= VK_F12)
        return juce::KeyPress::F1Key + (int) (vk - VK_F1);
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9)
        return juce::KeyPress::numberPad0 + (int) (vk - VK_NUMPAD0);
    switch (vk)
    {
        case VK_SPACE: return juce::KeyPress::spaceKey;
        case VK_RETURN: return juce::KeyPress::returnKey;
        case VK_TAB: return juce::KeyPress::tabKey;
        case VK_DELETE: return juce::KeyPress::deleteKey;
        case VK_BACK: return juce::KeyPress::backspaceKey;
        case VK_INSERT: return juce::KeyPress::insertKey;
        case VK_UP: return juce::KeyPress::upKey;
        case VK_DOWN: return juce::KeyPress::downKey;
        case VK_LEFT: return juce::KeyPress::leftKey;
        case VK_RIGHT: return juce::KeyPress::rightKey;
        case VK_HOME: return juce::KeyPress::homeKey;
        case VK_END: return juce::KeyPress::endKey;
        case VK_PRIOR: return juce::KeyPress::pageUpKey;
        case VK_NEXT: return juce::KeyPress::pageDownKey;
        case VK_ADD: return juce::KeyPress::numberPadAdd;
        case VK_SUBTRACT: return juce::KeyPress::numberPadSubtract;
        default: return 0;
    }
}

// The window holding the focus when it is in a hosted plugin's view inside one of LYNX's secondary windows (not the
// main window); nullptr otherwise
juce::Component* windowOfFocusedPluginView()
{
    const auto focus = GetFocus();
    if (focus == nullptr)
        return nullptr;
    // the nearest LYNX window up from the focus: a secondary top-level window, or a docked plugin frame (a native child
    // of the main window, issue #116); the main window itself (its panels) is not a plugin view
    auto* main = mainWindow().getComponent();
    juce::Component* window = nullptr;
    HWND root = nullptr;
    for (auto h = focus; h != nullptr && h != GetDesktopWindow() && window == nullptr; h = GetAncestor (h, GA_PARENT))
        for (int i = 0; i < juce::ComponentPeer::getNumPeers(); ++i)
            if (auto* peer = juce::ComponentPeer::getPeer (i); peer != nullptr && peer->getNativeHandle() == (void*) h)
            {
                window = &peer->getComponent();
                root = h;
            }
    if (window == nullptr)
        return nullptr; // (not a LYNX window)
    if (window == main)
        return nullptr;
    // a native child panel (the Channel rack, issue #116) is LYNX's own, not a plugin's view
    if (GetAncestor (root, GA_ROOT) != root && ! (bool) window->getProperties().getWithDefault ("lynxPluginWindow", false))
        return nullptr;
    if (focus != root)
    {
        // a native child: the plugin's view, unless it is a native text field
        wchar_t cls[64] = {};
        GetClassNameW (focus, cls, 63);
        const juce::String name (cls);
        return name.containsIgnoreCase ("edit") ? nullptr : window;
    }
    // JUCE's own focus: a hosted plugin's editor wrapper (it swallows every key)
    auto* focused = juce::Component::getCurrentlyFocusedComponent();
    auto* editor = focused != nullptr ? (dynamic_cast<juce::AudioProcessorEditor*> (focused) != nullptr ? dynamic_cast<juce::AudioProcessorEditor*> (focused)
                                                                                                      : focused->findParentComponentOfClass<juce::AudioProcessorEditor>())
                                      : nullptr;
    return editor != nullptr && dynamic_cast<juce::AudioPluginInstance*> (editor->getAudioProcessor()) != nullptr ? window : nullptr;
}

bool focusIsInPluginView()
{
    return windowOfFocusedPluginView() != nullptr;
}

HHOOK pluginKeyHook = nullptr;

// The key code JUCE gives a plain punctuation key (the typing keyboard's layouts use some); 0: not one
int punctuationKeyCode (WPARAM vk)
{
    if ((vk >= VK_OEM_1 && vk <= VK_OEM_3) || (vk >= VK_OEM_4 && vk <= VK_OEM_8))
        return (int) (MapVirtualKeyW ((UINT) vk, MAPVK_VK_TO_CHAR) & 0x7fffu);
    return 0;
}

int currentKeyMods()
{
    int mods = 0;
    if (GetKeyState (VK_CONTROL) & 0x8000) mods |= juce::ModifierKeys::ctrlModifier;
    if (GetKeyState (VK_SHIFT) & 0x8000) mods |= juce::ModifierKeys::shiftModifier;
    if (GetKeyState (VK_MENU) & 0x8000) mods |= juce::ModifierKeys::altModifier;
    return mods;
}

// A press of a plain key the plugin's option gives to the host
bool hostTakesPlainKey (WPARAM vk, juce::Component& window)
{
    if (plainKeyHandler == nullptr || plainKeysAllowed == nullptr)
        return false;
    const auto letterOrDigit = (vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9');
    const auto keyCode = letterOrDigit ? (int) vk : punctuationKeyCode (vk);
    const auto mods = currentKeyMods();
    if (keyCode == 0 || (mods & (juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::altModifier)) != 0)
        return false;
    return plainKeysAllowed (window) && plainKeyHandler (juce::KeyPress (keyCode, juce::ModifierKeys (mods), 0));
}

// A press of a key that runs one of LYNX's global actions (Space plays, F-keys open the panels...); plain letters and
// digits stay the plugin's
bool hostTakesShortcut (WPARAM vk)
{
    if (keyForwarder().handler == nullptr || vk == VK_ESCAPE)
        return false;
    const auto keyCode = juceKeyCodeOf (vk);
    if (keyCode == 0)
        return false;
    const auto mods = currentKeyMods();
    const auto plainCharacter = ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9'))
                                && (mods & (juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::altModifier)) == 0;
    return ! plainCharacter && keyForwarder().handler (juce::KeyPress (keyCode, juce::ModifierKeys (mods), 0));
}

// Which of the two took a press: its repeats go back to the same one
std::bitset<256> takenAsPlainKey;

bool hostTakesKey (WPARAM vk)
{
    auto* window = windowOfFocusedPluginView();
    if (window == nullptr || vk >= 256)
        return false;
    takenAsPlainKey[vk] = hostTakesPlainKey (vk, *window);
    return takenAsPlainKey[vk] || hostTakesShortcut (vk);
}

void hostKeyRepeats (WPARAM vk)
{
    // a held key repeats as over the main window (JUCE hands it keyPressed again; the typing piano's held notes
    // ignore it, a shortcut runs again)
    if (vk >= 256)
        return;
    if (takenAsPlainKey[vk])
    {
        const auto keyCode = (vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9') ? (int) vk : punctuationKeyCode (vk);
        if (plainKeyHandler != nullptr && keyCode != 0)
            plainKeyHandler (juce::KeyPress (keyCode, juce::ModifierKeys (currentKeyMods()), 0));
    }
    else if (const auto keyCode = juceKeyCodeOf (vk); keyCode != 0 && keyForwarder().handler != nullptr)
        keyForwarder().handler (juce::KeyPress (keyCode, juce::ModifierKeys (currentKeyMods()), 0));
}

HostKeyGate keyGate;

// A thread WH_GETMESSAGE hook (it was WH_KEYBOARD): it sees every keyboard message the thread takes from its queue,
// real keys and posted ones alike, so --plugwintest checks the same path without injected input. Only a message for
// the focused window counts: a key a plugin passes back up to its parent window is the plugin's answer, not a new press.
LRESULT CALLBACK pluginKeyProc (int code, WPARAM removal, LPARAM data)
{
    auto* msg = reinterpret_cast<MSG*> (data);
    if (code == HC_ACTION && removal == PM_REMOVE && msg != nullptr
        && (msg->message == WM_KEYDOWN || msg->message == WM_KEYUP || msg->message == WM_SYSKEYDOWN || msg->message == WM_SYSKEYUP)
        && msg->hwnd != nullptr && msg->hwnd == GetFocus())
    {
        const auto vk = msg->wParam;
        const auto down = msg->message == WM_KEYDOWN || msg->message == WM_SYSKEYDOWN;
        const auto repeat = (msg->lParam & (1 << 30)) != 0;
        if (keyGate.message ((std::size_t) vk, down, repeat, [vk] { return hostTakesKey (vk); }, [vk] { hostKeyRepeats (vk); }))
            msg->message = WM_NULL; // the host's: the plugin gets none of this press (no WM_CHAR either)
    }
    return CallNextHookEx (pluginKeyHook, code, removal, data);
}
} // namespace
#endif

void setSecondaryWindowKeyHandler (std::function<bool (const juce::KeyPress&)> handler)
{
    keyForwarder().handler = std::move (handler);
   #if JUCE_WINDOWS
    if (keyForwarder().handler != nullptr && pluginKeyHook == nullptr)
        pluginKeyHook = SetWindowsHookExW (WH_GETMESSAGE, pluginKeyProc, nullptr, GetCurrentThreadId());
    else if (keyForwarder().handler == nullptr && pluginKeyHook != nullptr)
    {
        UnhookWindowsHookEx (pluginKeyHook);
        pluginKeyHook = nullptr;
    }
   #endif
}

bool runSecondaryWindowKey (const juce::KeyPress& key)
{
    return keyForwarder().handler != nullptr && keyForwarder().handler (key);
}

void setPluginViewPlainKeyHandler (std::function<bool (juce::Component&)> allows, std::function<bool (const juce::KeyPress&)> first,
                                   std::function<bool (const juce::KeyPress&)> left)
{
    plainKeysAllowed = std::move (allows);
    plainKeyHandler = std::move (first);
    plainKeyLeftHandler = std::move (left);
}

void listenForKeysPluginLeaves (juce::Component& pluginEditor)
{
    static PluginLeftKeys listener;
    pluginEditor.addKeyListener (&listener); // (JUCE adds it once)
    for (auto* child : pluginEditor.getChildren())
        listenForKeysPluginLeaves (*child);
}

namespace
{
std::function<void (int, int)> linkedFocusHandler;

struct LinkedFocusListener final : juce::FocusChangeListener
{
    juce::Component::SafePointer<juce::Component> last;
    void globalFocusChanged (juce::Component* focused) override
    {
        if (auto* top = focused != nullptr ? focused->getTopLevelComponent() : nullptr)
            linkedWindowFocused (*top);
    }
};

LinkedFocusListener& linkedFocusListener()
{
    static LinkedFocusListener listener;
    return listener;
}
} // namespace

void markLinkedWindow (juce::Component& window, int channel, int insert)
{
    window.getProperties().set ("lynxLinkChannel", channel);
    window.getProperties().set ("lynxLinkInsert", insert);
}

void setLinkedWindowFocusHandler (std::function<void (int, int)> handler)
{
    linkedFocusHandler = std::move (handler);
    if (linkedFocusHandler != nullptr)
        juce::Desktop::getInstance().addFocusChangeListener (&linkedFocusListener());
    else
        juce::Desktop::getInstance().removeFocusChangeListener (&linkedFocusListener());
}

void linkedWindowFocused (juce::Component& window)
{
    auto& listener = linkedFocusListener();
    if (! window.getProperties().contains ("lynxLinkInsert"))
    {
        listener.last = nullptr; // (another window: coming back to this one is a new focus)
        return;
    }
    if (linkedFocusHandler == nullptr)
        return;
    if (listener.last.getComponent() == &window) // (the same window again: a click inside it, not a new focus)
        return;
    listener.last = &window;
    linkedFocusHandler ((int) window.getProperties()["lynxLinkChannel"], (int) window.getProperties()["lynxLinkInsert"]);
}

void keepAboveMainWindow (juce::Component& window)
{
    window.addKeyListener (&keyForwarder()); // (JUCE adds it once)
    applyRenderingChoice (window);
    auto& all = keepers();
    auto& keeper = all[&window];
    if (keeper == nullptr)
    {
        keeper = std::make_unique<OwnershipKeeper> (window);
        fitWindowOnScreen (window);
    }
    applyOwnership (window);
}

void fitWindowOnScreen (juce::Component& window)
{
    if (! window.isOnDesktop() || ! window.isVisible())
        return;
    auto* resizable = dynamic_cast<juce::ResizableWindow*> (&window);
    if (resizable != nullptr && (resizable->isFullScreen() || resizable->isMinimised()))
        return;

    juce::BorderSize<int> frame;
    if (auto* peer = window.getPeer())
        if (const auto size = peer->getFrameSizeIfPresent())
            frame = *size;

    const auto bounds = window.getBounds();
    const auto& displays = juce::Desktop::getInstance().getDisplays();
    const auto* display = displays.getDisplayForRect (frame.addedTo (bounds));
    if (display == nullptr)
        display = displays.getPrimaryDisplay();
    if (display == nullptr)
        return;
    const auto area = display->userArea;

    auto content = bounds;
    if (resizable != nullptr && resizable->isResizable())
    {
        auto w = juce::jmin (content.getWidth(), area.getWidth() - frame.getLeftAndRight());
        auto h = juce::jmin (content.getHeight(), area.getHeight() - frame.getTopAndBottom());
        if (auto* limits = resizable->getConstrainer())
        {
            w = juce::jmax (w, limits->getMinimumWidth());
            h = juce::jmax (h, limits->getMinimumHeight());
        }
        content.setSize (juce::jmax (1, w), juce::jmax (1, h));
    }
    const auto outer = frame.addedTo (content);
    const auto placed = windowfit::place ({ outer.getX(), outer.getY(), outer.getWidth(), outer.getHeight() },
                                          { area.getX(), area.getY(), area.getWidth(), area.getHeight() });
    content.setPosition (placed.x + frame.getLeft(), placed.y + frame.getTop());
    if (content != bounds)
        window.setBounds (content);
}

void fitAllWindowsOnScreen()
{
    auto& desktop = juce::Desktop::getInstance();
    for (int i = desktop.getNumComponents(); --i >= 0;)
        if (auto* window = dynamic_cast<juce::ResizableWindow*> (desktop.getComponent (i)))
            fitWindowOnScreen (*window);
}
namespace
{
// the usable screen of the main window (or the primary one), in logical pixels
juce::Rectangle<int> usableScreen()
{
    const auto& displays = juce::Desktop::getInstance().getDisplays();
    const juce::Displays::Display* display = nullptr;
    if (auto* main = juce::TopLevelWindow::getActiveTopLevelWindow())
        display = displays.getDisplayForRect (main->getScreenBounds());
    if (display == nullptr)
        display = displays.getPrimaryDisplay();
    return display != nullptr ? display->userArea : juce::Rectangle<int> (0, 0, 1280, 720);
}

// a window's frame (title bar and borders, logical px): this window's own once it is on the screen, else another
// LYNX window's (the same window manager draws them), else a typical native title bar
juce::BorderSize<int> windowFrame (juce::Component* window)
{
    auto frameOf = [] (juce::Component* c) -> std::optional<juce::BorderSize<int>> {
        if (c != nullptr)
            if (auto* peer = c->getPeer())
                if (const auto f = peer->getFrameSizeIfPresent(); f && f->getTop() > 0)
                    return *f;
        return std::nullopt;
    };
    if (const auto own = frameOf (window))
        return *own;
    for (int i = 0; i < juce::TopLevelWindow::getNumTopLevelWindows(); ++i)
        if (const auto other = frameOf (juce::TopLevelWindow::getTopLevelWindow (i)))
            return *other;
    return { 32, 8, 8, 8 };
}

class ScrollingContent final : public juce::Component,
                               private juce::ComponentListener
{
public:
    explicit ScrollingContent (std::unique_ptr<juce::Component> c) : content (c.release())
    {
        setName (content->getName());
        viewport.setViewedComponent (content, true); // (the viewport deletes it)
        viewport.setScrollBarThickness (12);
        addAndMakeVisible (viewport);
        content->addComponentListener (this);
        fit();
    }

    ~ScrollingContent() override
    {
        if (content != nullptr)
            content->removeComponentListener (this);
    }

    void resized() override { viewport.setBounds (getLocalBounds()); }

    // on the screen: fitted again with the window's real frame, and the window placed so it is all on the screen
    void parentHierarchyChanged() override
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<ScrollingContent> (this)] {
            if (safe != nullptr && safe->getPeer() != nullptr)
                safe->fit();
        });
    }

    void fit()
    {
        if (content == nullptr)
            return;
        const auto area = usableScreen();
        auto* window = getTopLevelComponent() != this ? getTopLevelComponent() : nullptr;
        const auto frame = windowFrame (window);
        const auto f = windowfit::scrollFit (content->getWidth(), content->getHeight(), area.getWidth(), area.getHeight(), frame.getLeftAndRight(),
                                             frame.getTopAndBottom(), viewport.getScrollBarThickness());
        viewport.setScrollBarsShown (f.scrollsV, f.scrollsH);
        setSize (f.w, f.h);
        if (window != nullptr && window->isOnDesktop())
            fitWindowOnScreen (*window);
    }

private:
    void componentMovedOrResized (juce::Component&, bool, bool wasResized) override
    {
        if (wasResized)
            fit(); // (the window follows its content's size)
    }

    juce::Component::SafePointer<juce::Component> content;
    juce::Viewport viewport;
};
} // namespace

std::unique_ptr<juce::Component> fitContentToScreen (std::unique_ptr<juce::Component> content)
{
    if (content == nullptr)
        return content;
    const auto area = usableScreen();
    const auto frame = windowFrame (nullptr);
    const auto f = windowfit::scrollFit (content->getWidth(), content->getHeight(), area.getWidth(), area.getHeight(), frame.getLeftAndRight(),
                                         frame.getTopAndBottom(), 12);
    if (! f.scrolls())
        return content;
    return std::make_unique<ScrollingContent> (std::move (content));
}

void matchWindowOrderToScreen()
{
    auto* owner = mainWindow().getComponent();
    if (owner == nullptr)
        return;

    // the windows above the main window, from the lowest up (raising them in that order keeps their order)
    std::vector<juce::Component*> above;
    auto& desktop = juce::Desktop::getInstance();
    for (int i = 0; i < desktop.getNumComponents(); ++i)
        if (auto* c = desktop.getComponent (i); c != owner && c->isVisible() && ! c->isAlwaysOnTop() && ! nativechild::isAttached (*c)
                                                 && isAboveMainWindow (*c))
            above.push_back (c);

   #if JUCE_WINDOWS
    std::map<HWND, int> depth; // 0 = the topmost window on the screen
    int d = 0;
    for (auto h = GetTopWindow (nullptr); h != nullptr; h = GetWindow (h, GW_HWNDNEXT))
        depth[h] = d++;
    const auto depthOf = [&depth] (juce::Component* c) {
        const auto it = depth.find ((HWND) c->getWindowHandle());
        return it == depth.end() ? std::numeric_limits<int>::max() : it->second;
    };
    std::stable_sort (above.begin(), above.end(), [&] (juce::Component* a, juce::Component* b) { return depthOf (a) > depthOf (b); });
   #endif

    // issue #116: the main window's native child windows (panels, docked plugin windows) first, in their stacking order,
    // so a drop lands on the one in front (JUCE looks for the target through its list of desktop windows, from the end)
    {
        auto children = nativechild::attachedChildren (owner->getWindowHandle());
        children.erase (std::remove_if (children.begin(), children.end(), [] (juce::Component* c) { return ! c->isVisible(); }), children.end());
        above.insert (above.begin(), children.begin(), children.end());
    }

    // JUCE's list already in that order after the main window: nothing to do (no window is touched)
    const auto indexOf = [&desktop] (juce::Component* c) {
        for (int i = 0; i < desktop.getNumComponents(); ++i)
            if (desktop.getComponent (i) == c)
                return i;
        return -1;
    };
    auto inOrder = true;
    auto last = indexOf (owner);
    for (auto* c : above)
    {
        const auto at = indexOf (c);
        inOrder = inOrder && at > last;
        last = at;
    }
    if (inOrder)
        return;

    for (auto* c : above)
        c->toFront (false);
}

} // namespace lynx
