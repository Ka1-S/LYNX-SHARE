---
source: edc71061e6
---
LYNX SHARE 用 C++20 和 JUCE 8 编写，用 CMake 构建。源码位于 [github.com/Ka1-S/LYNX-SHARE](https://github.com/Ka1-S/LYNX-SHARE)，采用 GNU Affero General Public License v3.0 许可。发布文件由 GitHub Actions 根据打过标签的源码构建，所以只有在想修改代码或自己构建时，才需要看这一页。

## 在 Linux 上构建

1. 安装下面列出的依赖。
2. 用 `git clone https://github.com/Ka1-S/LYNX-SHARE.git lynx` 获取源码，然后进入 `lynx` 文件夹。
3. 用 `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release` 配置。
4. 用 `cmake --build build` 构建。

Ubuntu 24.04 上的依赖：

```
sudo apt install build-essential cmake ninja-build pkg-config libasound2-dev libjack-jackd2-dev libx11-dev \
  libxrandr-dev libxinerama-dev libxcursor-dev libxext-dev libxcomposite-dev libfreetype-dev libfontconfig1-dev libgl-dev
```

## 在 Windows 上用 Visual Studio 构建

需要安装了“使用 C++ 的桌面开发”的 Visual Studio 2022 Build Tools、CMake 3.22 或更新版本，以及 Git。

```
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

## 在 WSL 或 Linux 上构建 Windows 版

项目本身就是这样开发的：Windows 程序用 Zig 交叉编译，不需要 Visual Studio。

```
./scripts/build-windows-zig.sh
```

结果是 `build-win/LYNX.exe`。

| 要求 | 版本 | 默认位置 |
|---|---|---|
| Zig | 0.14.1 | `~/opt/zig-x86_64-linux-0.14.1` |
| JUCE | 8.0.4 | `~/opt/juce-8.0.4` |

JUCE 可以这样获取：

```
git clone --depth 1 -b 8.0.4 https://github.com/juce-framework/JUCE ~/opt/juce-8.0.4
```

如果你的路径不同，请设置环境变量 `ZIG` 和 `JUCE_DIR`。

## 在 macOS 上构建

需要 Xcode 命令行工具（`xcode-select --install`）、CMake 和 Ninja，例如通过 `brew install cmake ninja` 安装。

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build
```

`CMAKE_OSX_ARCHITECTURES="arm64;x86_64"` 会生成通用应用。省略它则只为当前这台 Mac 构建。

## 运行核心测试

核心部分（模型、音序器、乐器、效果器和 `.flp` 导入）是纯 C++，不依赖 JUCE，用任何 C++20 编译器都能测试。

```
./scripts/build-local.sh ["folder with .flp files"]
```

运行必须以 `N checks, 0 failures` 结束。如果给它 FL Studio 2026 安装目录下的 `Data` 文件夹，它还会解析随 FL Studio 附带的 `.flp` 工程。该脚本还会构建两个小工具：转储 `.flp` 的 `build-local/flpdump`，以及 `build-local/renderwav`。

构建之后，程序可以在没有音频设备的情况下自检：

```
LYNX.exe --selftest report.txt
```

报告以 `result=OK` 结尾。更多选项见[命令行](/wiki/command-line/)页面。

## 源码目录

| 文件夹 | 内容 |
|---|---|
| `src/core/` | 模型、音序器、乐器、效果器和 `.flp` 导入。纯 C++，不依赖 JUCE 即可测试 |
| `src/flp/` | `.flp` 格式的读取器 |
| `src/engine/` | 音频引擎：设备、图、采样、插件、工程文件 |
| `src/ui/` | 界面：通道机架、钢琴卷帘、播放列表、混音台、各个窗口 |
| `third_party/` | CLAP 和 WavPack 解码器 |
| `tests/` | 核心测试 |
| `tools/` | `flpdump`、`renderwav`、`comparewav`、`make_logo` |
| `docs/` | 架构、现状、路线图，以及对 FL Studio 行为的分析 |

## 须知

- JUCE 8 官方不支持 MinGW。Zig 构建脚本绕开了这些差异。相关修复在 `cmake/mingw/` 和 `src/platform/MingwFixes.cpp` 中。
- 一次只运行一个 `build-windows-zig.sh`。同时运行两个 Zig 构建可能会卡住。若要在不同的工作树中并行构建，请用 `ZIG_GLOBAL_CACHE_DIR` 和 `ZIG_LOCAL_CACHE_DIR` 给每个构建单独的缓存。
- 有些引擎会从你自己安装的 FL Studio 中读取内置声音和表格。仓库里没有任何来自 FL Studio 的内容。
- `docs/` 中的部分文档仍是葡萄牙语，正在翻译。
- 如果你分发修改后的版本，包括作为网络服务提供，许可证要求你以相同的许可证公布其源码。

## 相关页面

- [完整的开发环境 (SETUP_DEV.md)](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/SETUP_DEV.md)
- [Linux 上的 LYNX SHARE (LINUX.md)](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/LINUX.md)
- [仓库及其 README](https://github.com/Ka1-S/LYNX-SHARE)
- [命令行](/wiki/command-line/)
- [文件格式](/wiki/file-formats/)
