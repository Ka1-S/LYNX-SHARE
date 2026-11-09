// Issue #96's run-time check, no injected input (LYNX_PLUGWIN_KEYS=1 with --plugwintest): keyboard messages are posted to
// the plugin's own focused native view, whose window procedure is subclassed to count what actually reaches it. The
// hook is a WH_GETMESSAGE one (round 187a; WH_KEYBOARD never saw posted keys), so this is the path real keys take.
// Controls: F12 runs LYNX's shortcut (its repeat too), and with the option off the plugin gets every key.
#include "ui/PluginKeysTest.h"

#if JUCE_WINDOWS
 #include "ui/PluginWindow.h"
 #include "ui/WindowOwner.h"
 #include "engine/PluginSandbox.h"
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>

namespace lynx
{
namespace
{
struct State
{
    int phase = 0, down = 0, up = 0, plainCalls = 0, leftCalls = 0, shortcutCalls = 0;
    bool plainAnswer = true, ok = true;
    HWND view = nullptr, field = nullptr;
    WNDPROC proc = nullptr;
} state;

LRESULT CALLBACK countKeys (HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_KEYDOWN) ++state.down;
    if (m == WM_KEYUP) ++state.up;
    return CallWindowProcW (state.proc, h, m, w, l);
}

HWND deepestChild (HWND parent)
{
    for (auto c = GetWindow (parent, GW_CHILD); c != nullptr; c = GetWindow (c, GW_HWNDNEXT))
        if (IsWindowVisible (c))
            return deepestChild (c);
    return parent;
}

void postKey (HWND to, WPARAM vk, bool down, bool repeat)
{
    const auto l = (LPARAM) (1u | (MapVirtualKeyW ((UINT) vk, MAPVK_VK_TO_VSC) << 16) | (repeat || ! down ? 1u << 30 : 0u) | (down ? 0u : 1u << 31));
    PostMessageW (to, down ? WM_KEYDOWN : WM_KEYUP, vk, l);
}

void postPress (HWND to, WPARAM vk)
{
    postKey (to, vk, true, false);
    postKey (to, vk, true, true);
    postKey (to, vk, false, false);
}

void attach (HWND h)
{
    state.down = state.up = 0;
    state.proc = (WNDPROC) SetWindowLongPtrW (h, GWLP_WNDPROC, (LONG_PTR) countKeys);
    SetFocus (h);
}
} // namespace

bool pluginKeysTestTick (juce::AudioProcessorEditor& editor, const std::function<void (bool)>& setOption, const std::function<void (const juce::String&)>& step)
{
    const auto check = [&step] (const char* name, int wantDown, int wantUp, int wantPlain) {
        const auto good = state.down == wantDown && state.up == wantUp && (wantPlain < 0 || state.plainCalls == wantPlain) && GetFocus() != nullptr;
        state.ok = state.ok && good;
        step (juce::String ("keys_") + name + " view_down=" + juce::String (state.down) + " view_up=" + juce::String (state.up)
              + " host_asked=" + juce::String (state.plainCalls) + " came_back_unused=" + juce::String (state.leftCalls) + " expected=" + juce::String (wantDown) + "/" + juce::String (wantUp) + "/"
              + juce::String (wantPlain) + (good ? " OK" : " FAILED"));
        state.down = state.up = state.plainCalls = state.leftCalls = 0;
    };
    const auto root = (HWND) editor.getTopLevelComponent()->getWindowHandle();
    // LYNX_PLUGWIN_KEYS=real: real keys from outside; the view keeps its window procedure and every change is reported
    if (juce::SystemStats::getEnvironmentVariable ("LYNX_PLUGWIN_KEYS", {}) == "real")
    {
        static int seenPlain = -1, seenLeft = -1;
        if (state.phase++ == 0)
        {
            setSecondaryWindowKeyHandler ([] (const juce::KeyPress&) { ++state.shortcutCalls; return false; });
            setPluginViewPlainKeyHandler ([] (juce::Component& w) { return PluginWindows::letsHostProcessKeys (w); },
                                          [] (const juce::KeyPress&) { ++state.plainCalls; return true; },
                                          [] (const juce::KeyPress&) { ++state.leftCalls; return true; });
            setOption (juce::SystemStats::getEnvironmentVariable ("LYNX_PLUGWIN_KEYS_OPTION", {}) == "1");
        }
        if (state.plainCalls != seenPlain || state.leftCalls != seenLeft)
        {
            seenPlain = state.plainCalls;
            seenLeft = state.leftCalls;
            wchar_t cls[64] = {};
            GetClassNameW (GetFocus(), cls, 63);
            step ("keys_real host_first=" + juce::String (seenPlain) + " came_back_unused=" + juce::String (seenLeft) + " shortcuts=" + juce::String (state.shortcutCalls)
                  + " focus=" + juce::String (cls));
        }
        return true;
    }
    // a plugin in another process (issue #99's case): neither path may touch keys that reach LYNX's side of its window
    if (auto* instance = dynamic_cast<juce::AudioPluginInstance*> (editor.getAudioProcessor()); instance != nullptr && sandboxedPluginProcessId (*instance) != 0)
    {
        switch (state.phase++)
        {
            case 0:
                setPluginViewPlainKeyHandler ([] (juce::Component& w) { return PluginWindows::letsHostProcessKeys (w); },
                                              [] (const juce::KeyPress&) { ++state.plainCalls; return true; },
                                              [] (const juce::KeyPress&) { ++state.leftCalls; return true; });
                setSecondaryWindowKeyHandler ([] (const juce::KeyPress&) { return false; });
                setOption (true); // (a project can carry it on)
                return true;
            case 1:
                SetFocus (root);
                editor.grabKeyboardFocus();
                postPress (root, 'Q');
                return true;
            default:
            {
                const auto good = state.plainCalls == 0 && state.leftCalls == 0;
                step ("keys_separate_process host_asked=" + juce::String (state.plainCalls) + " came_back_unused=" + juce::String (state.leftCalls)
                      + " expected=0/0 " + (good ? "OK" : "FAILED"));
                setOption (false);
                setPluginViewPlainKeyHandler (nullptr, nullptr, nullptr);
                setSecondaryWindowKeyHandler (nullptr);
                step (good ? "keys_result=OK" : "keys_result=FAILED");
                return false;
            }
        }
    }
    switch (state.phase++)
    {
        case 0:
        {
            setSecondaryWindowKeyHandler ([] (const juce::KeyPress&) { ++state.shortcutCalls; return true; });
            setPluginViewPlainKeyHandler ([] (juce::Component& w) { return PluginWindows::letsHostProcessKeys (w); },
                                          [] (const juce::KeyPress&) { ++state.plainCalls; return state.plainAnswer; },
                                          [] (const juce::KeyPress&) { ++state.leftCalls; return true; });
            state.view = deepestChild (root);
            wchar_t cls[64] = {};
            GetClassNameW (state.view, cls, 63);
            step ("keys_view_class=" + juce::String (cls) + " is_root=" + juce::String ((int) (state.view == root)));
            attach (state.view);
            postPress (state.view, VK_F12); // a shortcut: down, one repeat, up
            return true;
        }
        case 1:
        {
            // LYNX's shortcut over the plugin (round 165q): the press and its repeat run it, the plugin sees none of it
            const auto good = state.shortcutCalls == 2 && state.down == 0 && state.up == 0;
            state.ok = state.ok && good;
            step ("keys_shortcut_with_repeat shortcuts=" + juce::String (state.shortcutCalls) + " view_down=" + juce::String (state.down)
                  + " view_up=" + juce::String (state.up) + " expected=2/0/0" + (good ? " OK" : " FAILED"));
            state.down = state.up = state.plainCalls = state.leftCalls = state.shortcutCalls = 0;
            postPress (state.view, 'Q'); // the option off, FL's default
            return true;
        }
        case 2:
            // (came_back_unused: what the plugin's view passed back, the plugin's choice; FL plays those too)
            check ("option_off", 2, 1, 0);
            setOption (true);
            postPress (state.view, 'Q');
            return true;
        case 3:
            // the host takes the press: asked on the key-down and again on its repeat, the plugin sees nothing
            check ("option_on", 0, 0, 2);
            state.plainAnswer = false;
            postPress (state.view, 'W');
            return true;
        case 4:
            check ("option_on_host_declines", 2, 1, 1);
            SetWindowLongPtrW (state.view, GWLP_WNDPROC, (LONG_PTR) state.proc);
            state.field = CreateWindowExW (0, L"EDIT", L"", WS_CHILD | WS_VISIBLE, 0, 0, 60, 20, root, nullptr, nullptr, nullptr);
            state.plainAnswer = true;
            attach (state.field);
            postPress (state.field, 'Q');
            return true;
        case 5:
            check ("native_text_field_option_on", 2, 1, 0);
            SetWindowLongPtrW (state.field, GWLP_WNDPROC, (LONG_PTR) state.proc);
            DestroyWindow (state.field);
            // LYNX_PLUGWIN_SHOT=<png>: the Wrapper settings page with the option on (FL | LYNX review)
            if (const auto shot = juce::SystemStats::getEnvironmentVariable ("LYNX_PLUGWIN_SHOT", {}); shot.isNotEmpty())
                step ("keys_settings_shot=" + juce::String ((int) PluginWindows::saveSettingsPageShot (*editor.getTopLevelComponent(), juce::File (shot))));
            setOption (false);
            // (round 187a: the header again with the Typing keyboard priority button on, FL's default; taken after the
            // key checks, since showing the page moves the focus)
            if (const auto shot = juce::SystemStats::getEnvironmentVariable ("LYNX_PLUGWIN_SHOT", {}); shot.isNotEmpty())
                PluginWindows::saveSettingsPageShot (*editor.getTopLevelComponent(), juce::File (shot.upToLastOccurrenceOf (".", false, false) + "-plugin-first.png"));
            setPluginViewPlainKeyHandler (nullptr, nullptr, nullptr);
            setSecondaryWindowKeyHandler (nullptr);
            step (state.ok ? "keys_result=OK" : "keys_result=FAILED");
            return false;
        default:
            return false;
    }
}
} // namespace lynx
#else
namespace lynx
{
bool pluginKeysTestTick (juce::AudioProcessorEditor&, const std::function<void (bool)>&, const std::function<void (const juce::String&)>& step)
{
    step ("keys_result=NOT_APPLICABLE (the keyboard hook is Windows' only)");
    return false;
}
} // namespace lynx
#endif
