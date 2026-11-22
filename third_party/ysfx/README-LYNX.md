# ysfx in LYNX SHARE

ysfx 0.0.47 from https://github.com/JoepVanlier/ysfx at commit 5c3452fee62583aa3d1b7e877d0c758c4024af89 (2026-08-19), with
its submodule dr_libs at f13cbcfd. Licence: Apache-2.0 (`LICENSE`); the bundled parts keep their own (`thirdparty/WDL/LICENSE.txt`,
`sources/eel2-gas/LICENSE.txt`, `thirdparty/dr_libs/LICENSE`).

Kept: `include/`, `sources/` (without `lice_stb/`), `thirdparty/WDL/source/WDL` without `lice/`, `swell/` and
`eel2/asm-nseel-x64-sse.asm` (the nasm original of `sources/eel2-gas`, used by MSVC builds only), and `dr_wav.h`, `dr_flac.h` of
dr_libs. Left out: the JUCE plugin, @gfx drawing (LICE / SWELL, stb), tests, tools, nlohmann json, clap-juce-extensions.
LYNX builds it with `YSFX_NO_GFX` (CMakeLists.txt target `lynx_ysfx`, scripts/build-windows-zig.sh).

LYNX's change (marked "LYNX" in the files):
- `include/ysfx.h`, `sources/ysfx.hpp`, `sources/ysfx.cpp`: `ysfx_lynx_set_path_filter`, a host filter consulted by
  `ysfx_find_data_file` (what `file_open`, file sliders and `gfx_loadimg` open). LYNX lets scripts read only inside the JSFX
  folders, their Data folders and the open project's folder (src/engine/JsfxFormat.cpp). Without a filter ysfx behaves as
  upstream.
