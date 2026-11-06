"""LYNX-bridge32.exe (round 176a): JUCE 8 passes lambdas where Win32 wants a __stdcall callback. MSVC converts a lambda to
any calling convention; clang, on 32-bit x86, only to the default (cdecl) one, so those calls don't compile. This makes
copies of the affected modules under <out> with each such lambda turned into a static CALLBACK function. 64-bit builds
are unaffected (one calling convention). Copies are only rewritten when their content changes (object cache).

usage: bridge32_patch_juce.py <juce modules> <patched modules (may not exist)> <out modules>"""
import os, shutil, sys, filecmp

src, patched, out = sys.argv[1:4]

# (module, file inside it, text to find, replacement)
PATCHES = [
    ("juce_core", "native/juce_PlatformTimer_windows.cpp",
     """        const auto callback = [] (UINT, UINT, DWORD_PTR context, DWORD_PTR, DWORD_PTR)
        {
            reinterpret_cast<PlatformTimerListener*> (context)->onTimerExpired();
        };
""",
     """        struct LynxCallback { static void CALLBACK call (UINT, UINT, DWORD_PTR context, DWORD_PTR, DWORD_PTR)
        {
            reinterpret_cast<PlatformTimerListener*> (context)->onTimerExpired();
        } };
        const auto callback = &LynxCallback::call;
"""),
    ("juce_gui_basics", "native/juce_NativeMessageBox_windows.cpp",
     "config.pfCallback     = [] (HWND hwnd, UINT msg, WPARAM, LPARAM, LONG_PTR lpRefData)\n",
     "config.pfCallback     = [] (HWND hwnd, UINT msg, WPARAM, LPARAM, LONG_PTR lpRefData) __stdcall\n"),
]

def base_of(module):
    p = os.path.join(patched, module)
    return p if os.path.isfile(os.path.join(p, module + ".cpp")) else os.path.join(src, module)

for module in sorted({p[0] for p in PATCHES}):
    tmp = os.path.join(out, module + ".new")
    shutil.rmtree(tmp, ignore_errors=True)
    shutil.copytree(base_of(module), tmp)
    for m, rel, old, new in PATCHES:
        if m != module:
            continue
        path = os.path.join(tmp, rel)
        text = open(path, encoding="utf-8").read()
        if old not in text:
            sys.exit("bridge32_patch_juce: pattern not found in %s/%s" % (module, rel))
        open(path, "w", encoding="utf-8").write(text.replace(old, new))
    dest = os.path.join(out, module)
    if os.path.isdir(dest):
        cmp = filecmp.dircmp(tmp, dest)
        def changed(c):
            return c.diff_files or c.left_only or c.right_only or any(changed(s) for s in c.subdirs.values())
        if not changed(cmp):
            shutil.rmtree(tmp)
            continue
        shutil.rmtree(dest)
    os.rename(tmp, dest)
