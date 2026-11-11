---
source: a6d9063ec3
---
LYNX SHARE 是一款免费开源的 DAW，支持 Windows、macOS 和 Linux。它以样式为核心：你在通道机架和钢琴卷帘中编写样式，在播放列表中编排，在混音台中混音。它能打开 FL Studio 工程（`.flp`）和 Ableton Live 套装（`.als`），并以自己的 `.lynx` 格式保存。

它目前是 alpha 版。可能会有错误，请经常保存。

## 让它运行起来

1. 查看下面的表格，确认你的系统受支持。
2. 下载并安装。[安装](/wiki/install/)中有各系统的步骤。
3. 启动它。一个简短的设置向导会选择语言、测试音频输出，并列出插件文件夹，这些文件夹随后会在后台扫描。你可以跳过它，之后用 `选项 › 设置向导（语言、音频、插件文件夹）...` 重新打开。
4. 如果听不到声音，请看[音频设置](/wiki/audio-setup/)。
5. 阅读[窗口导览](/wiki/tour/)，然后做出[你的第一首歌](/wiki/first-song/)。

## 你需要什么

| 系统 | 要求 |
|---|---|
| Windows | Windows 10 或 11，64 位 |
| macOS | macOS 11 Big Sur 或更新版本，Intel 或 Apple 芯片 |
| Linux | x86_64，glibc 2.35 或更新（Ubuntu 22.04+、Debian 12+、Fedora 36+、当前的 Arch 或 Manjaro），X11 或 XWayland |

你不需要音频接口。LYNX SHARE 通过你的系统已在使用的声音设备播放。你也不需要 FL Studio。如果它装在同一台电脑上，LYNX SHARE 会找到它的采样，并为 `.flp` 工程使用相同的插件文件。

第三方插件是可选的。LYNX SHARE 有自己的乐器和效果器，列在[目录](/instruments-and-effects/)中。

## 保存你的工作

`Ctrl+S` 保存，`Ctrl+Shift+S` 另存为新名称，`Ctrl+Alt+S` 保存新版本（Song_2、Song_3，依此类推）。还有自动保存，可通过 `文件 › 打开自动保存文件夹` 和 `文件 › 恢复到上一个备份...` 访问。

`文件 › 从模板新建` 提供四个起点：空、Basic beat、Trap with 808 和 House。

## 演示工程

Windows 安装程序和 zip、macOS 的 `.dmg` 以及 Linux 的 `.tar.gz` 都包含一个演示工程 `Demo.lynx`。用 `文件 › 打开...`（`Ctrl+O`）打开它。在 Windows 上，它位于安装文件夹中，紧挨着 `LYNX.exe`。

你也可以在终端中生成它。见[命令行](/wiki/command-line/)。

```
LYNX.exe --showcase out.lynx
```

## 接下来去哪里

| 你想要 | 阅读 |
|---|---|
| 了解每个窗口 | [通道机架](/wiki/channel-rack/)、[钢琴卷帘](/wiki/piano-roll/)、[播放列表](/wiki/playlist/)、[混音台](/wiki/mixer/)、[浏览器](/wiki/browser/) |
| 打开来自其他程序的工程 | [从 FL Studio 转来](/wiki/coming-from-fl-studio/)、[从 Ableton Live 转来](/wiki/coming-from-ableton/) |
| 使用你的 VST、CLAP、LV2 或 AU 插件 | [插件](/wiki/plugins/) |
| 查某个按键 | [快捷键](/wiki/shortcuts/) |
| 解决问题 | [故障排除](/wiki/troubleshooting/)和[常见问题](/faq/) |

## 须知

- 程序文件在 Windows 上叫 `LYNX.exe`，在 macOS 和 Linux 上叫 `LYNX`。设置文件夹也叫 `LYNX`。
- 在 Windows 和 macOS 上，首次启动时可能会出现系统警告，因为 Windows 版本尚未进行代码签名，macOS 版本也尚未经过公证。[安装](/wiki/install/)说明了该点哪里。
- 在 LYNX SHARE 不支持其语言的系统上，界面默认是英文。你可以在 `选项 › 常规设置...` 中更改。新语言在重启后生效。
- FL Studio 工程中的 FLEX 包是加密的，永远不会被打开。这些通道会得到一个按预设名称选定的合成器音色。

## 相关

- [安装](/wiki/install/)
- [音频设置](/wiki/audio-setup/)
- [窗口导览](/wiki/tour/)
- [你的第一首歌](/wiki/first-song/)
- [下载](/download/)
