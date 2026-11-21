#include "platform/NativeEmbed.h"

#include <juce_audio_processors/juce_audio_processors.h>

#if JUCE_WINDOWS
 #include <windows.h>
#elif JUCE_MAC
 #include <objc/message.h>
 #include <objc/runtime.h>
#endif

namespace lynx::platform
{
#if JUCE_WINDOWS
struct NativeEmbed::Impl
{
    explicit Impl (juce::Component& o) : owner (o) { owner.addAndMakeVisible (holder); }
    ~Impl()
    {
        holder.setHWND (nullptr);
        if (container != nullptr)
            DestroyWindow ((HWND) container);
    }

    bool create()
    {
        if (container != nullptr)
            return true;
        // the child window needs a real window as parent: only once on screen
        auto* peer = owner.getPeer();
        if (peer == nullptr)
            return false;
        const auto scale = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->scale;
        container = CreateWindowExW (0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0,
                                     juce::roundToInt (owner.getWidth() * scale), juce::roundToInt (owner.getHeight() * scale),
                                     (HWND) peer->getNativeHandle(), nullptr, nullptr, nullptr);
        holder.setHWND (container);
        holder.setBounds (owner.getLocalBounds());
        return container != nullptr;
    }

    void* handle() const noexcept { return container; }
    unsigned long x11() const noexcept { return 0; }
    void resized() { holder.setBounds (owner.getLocalBounds()); }

    juce::Component& owner;
    juce::HWNDComponent holder;
    void* container = nullptr;
};
#elif JUCE_MAC
struct NativeEmbed::Impl
{
    explicit Impl (juce::Component& o) : owner (o) { owner.addAndMakeVisible (holder); }
    ~Impl() { holder.setView (nullptr); }

    bool create()
    {
        if (view != nullptr)
            return true;
        if (owner.getPeer() == nullptr)
            return false;
        // [[NSView alloc] init]; NSViewComponent retains it and keeps its frame on the component's bounds
        using Send = id (*) (id, SEL);
        auto* cls = (id) objc_getClass ("NSView");
        auto obj = ((Send) objc_msgSend) (((Send) objc_msgSend) (cls, sel_registerName ("alloc")), sel_registerName ("init"));
        if (obj == nullptr)
            return false;
        holder.setView (obj);
        ((void (*) (id, SEL)) objc_msgSend) (obj, sel_registerName ("release"));
        view = obj;
        holder.setBounds (owner.getLocalBounds());
        return true;
    }

    void* handle() const noexcept { return view; }
    unsigned long x11() const noexcept { return 0; }
    void resized() { holder.setBounds (owner.getLocalBounds()); }

    juce::Component& owner;
    juce::NSViewComponent holder;
    void* view = nullptr;
};
#elif JUCE_LINUX || JUCE_BSD
struct NativeEmbed::Impl
{
    explicit Impl (juce::Component& o) : owner (o) { owner.addAndMakeVisible (holder); }
    ~Impl() { holder.removeClient(); }

    bool create()
    {
        if (window != 0)
            return true;
        if (owner.getPeer() == nullptr)
            return false;
        holder.setBounds (owner.getLocalBounds());
        window = holder.getHostWindowID();
        return window != 0;
    }

    void* handle() const noexcept { return (void*) (std::uintptr_t) window; }
    unsigned long x11() const noexcept { return window; }
    void resized()
    {
        holder.setBounds (owner.getLocalBounds());
        holder.updateEmbeddedBounds();
    }

    juce::Component& owner;
    juce::XEmbedComponent holder { true, false };
    unsigned long window = 0;
};
#else
struct NativeEmbed::Impl
{
    explicit Impl (juce::Component&) {}
    bool create() { return false; }
    void* handle() const noexcept { return nullptr; }
    unsigned long x11() const noexcept { return 0; }
    void resized() {}
};
#endif

NativeEmbed::NativeEmbed() : impl (std::make_unique<Impl> (*this)) {}
NativeEmbed::~NativeEmbed() = default;

bool NativeEmbed::create() { return impl->create(); }
bool NativeEmbed::isCreated() const noexcept { return impl->handle() != nullptr; }
void* NativeEmbed::nativeHandle() const noexcept { return impl->handle(); }
unsigned long NativeEmbed::x11Window() const noexcept { return impl->x11(); }
void NativeEmbed::resized() { impl->resized(); }

const char* NativeEmbed::clapWindowApi() noexcept
{
   #if JUCE_WINDOWS
    return "win32";
   #elif JUCE_MAC
    return "cocoa";
   #else
    return "x11";
   #endif
}

namespace
{
double primaryScale()
{
    const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
    return display != nullptr ? display->scale : 1.0; // (as the editors read it before round 190oh)
}
} // namespace

void setEditorScaling (juce::Component& editor, bool dpiUnaware, bool scaleDims)
{
    editor.getProperties().set ("lynxDpiUnaware", dpiUnaware);
    editor.getProperties().set ("lynxScaleEditorDimensions", dpiUnaware || scaleDims);
    if (auto* processorEditor = dynamic_cast<juce::AudioProcessorEditor*> (&editor))
        processorEditor->setScaleFactor (1.0f); // (each editor sizes itself again with the marks)
}

double editorSizeScale (const juce::Component& editor)
{
    return (bool) editor.getProperties()["lynxScaleEditorDimensions"] ? 1.0 : primaryScale();
}

double editorContentScale (const juce::Component& editor)
{
    return (bool) editor.getProperties()["lynxDpiUnaware"] ? 1.0 : primaryScale();
}

namespace
{
#if JUCE_WINDOWS
using GetDpiContext = void* (WINAPI*) (HWND);  // GetWindowDpiAwarenessContext
using GetAwareness = int (WINAPI*) (void*);    // GetAwarenessFromDpiAwarenessContext (0 = DPI_AWARENESS_UNAWARE)
struct DpiWalk
{
    GetDpiContext getContext = nullptr;
    GetAwareness getAwareness = nullptr;
    int windows = 0, unaware = 0;
};

BOOL CALLBACK countDpiWindow (HWND h, LPARAM l)
{
    auto& w = *reinterpret_cast<DpiWalk*> (l);
    ++w.windows;
    if (w.getAwareness (w.getContext (h)) == 0)
        ++w.unaware;
    return TRUE;
}
#endif
} // namespace

int dpiUnawareWindowCount (void* nativeWindow, int& windows)
{
    windows = 0;
#if JUCE_WINDOWS
    if (nativeWindow == nullptr)
        return -1;
    auto* user32 = GetModuleHandleW (L"user32.dll");
    DpiWalk walk;
    walk.getContext = reinterpret_cast<GetDpiContext> (reinterpret_cast<void*> (GetProcAddress (user32, "GetWindowDpiAwarenessContext")));
    walk.getAwareness = reinterpret_cast<GetAwareness> (reinterpret_cast<void*> (GetProcAddress (user32, "GetAwarenessFromDpiAwarenessContext")));
    if (walk.getContext == nullptr || walk.getAwareness == nullptr)
        return -1;
    countDpiWindow (static_cast<HWND> (nativeWindow), (LPARAM) &walk);
    EnumChildWindows (static_cast<HWND> (nativeWindow), countDpiWindow, (LPARAM) &walk);
    windows = walk.windows;
    return walk.unaware;
#else
    (void) nativeWindow;
    return -1;
#endif
}
} // namespace lynx::platform
