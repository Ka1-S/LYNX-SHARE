---
source: e1ff018baf
---
LYNX SHARE 把工程保存为 `.lynx` 文件。它也能打开其他程序的工程，加载常见的音频文件，并导出供其他程序使用的音频、MIDI 和工程。下表列出了 0.1.31 alpha 的所有格式。

从其他程序的格式打开的工程属于导入。此时 `文件 › 保存` 会询问把 `.lynx` 文件写到哪里，原始文件永远不会被改动。

## 打开其他程序的工程

1. 选择 `文件 › 打开...`（`Ctrl+O`）。
2. 选取 `.flp`、`.als`、`.flm`、`.dawproject`、`.band`、`.blx`、`.mid` 或 `.zip` 文件。
3. 选择 `帮助 › 上次工程导入报告`，查看哪些内容被近似处理或舍去。
4. 选择 `文件 › 保存`，并选择 `.lynx` 文件的写入位置。

## 格式

| 格式 | 打开 | 保存 / 导出 | 说明 |
|---|---|---|---|
| `.lynx` | `文件 › 打开...` | `文件 › 保存`，带自动保存 | LYNX SHARE 自己的工程格式（JSON）。`文件 › 为旧版 LYNX 保存副本...` 会写出一个 0.1.27 或 0.1.28 能打开的副本，并列出该版本无法保留的内容 |
| `.zip` | `文件 › 打开...` 把它解压到一个文件夹，然后打开其中的工程 | `文件 › 导出 › 压缩循环包...` | 也能打开 FL Studio 的压缩循环包 |
| `.lynxpack` | 显示摘要后打开。其中的一部分会合并到宿主的工程 | `文件 › 在线会话 › 发送工程...` | 在[在线会话](/wiki/online-sessions/)中发送的工程或其中一部分。第三方插件的部分以音频渲染加插件设置的形式传送 |
| `.flp` | `文件 › 打开...` | `文件 › 导出 › FL Studio 工程...`，即 FL Studio 2026 工程或压缩循环包 | 原生插件在重建的引擎上播放，精确程度各不相同。FLEX 包永远不会被打开。导出时，仅 LYNX 有的乐器和效果器会被渲染为音频，或换成最接近的 FL 插件，调制器则会被舍去。见[从 FL Studio 转来](/wiki/coming-from-fl-studio/) |
| `.flm` | `文件 › 打开...` | `文件 › 将副本保存为 FL Studio Mobile 工程 (.flm)...` | FL Studio Mobile 工程。部分导入：除 MiniSynth、Transistor Bass、3x Osc、GMSynth、SuperSaw、DW Sampler、Slicer 和 DirectWave 之外的乐器用内置合成器播放。导出是一份副本：保留音符、速度、混音台、FL Studio Mobile 自带的乐器、DW Sampler、3x Osc、鼓组、效果器，以及混音台音量和声像、效果器、DW Sampler 和 GMSynth 的自动化，采样放在旁边。副本中，其他乐器的音符保留在 MiniSynth 上，没有 FLM 模块的效果器和其他目标（合成器自身的旋钮、第三方插件）的自动化会被省略；保存前会有一个窗口列出这些内容。 |
| `.als` | `文件 › 打开...`，Live 11 和 12 的套装 | `文件 › 导出 › Ableton Live 套装 (.als)...`，适用于 Live 12 或 Live 11 | 导入时，Live 中没有 LYNX SHARE 对应项的设备会被丢弃。导出时，LYNX SHARE 自己的乐器和效果器不会被转换（Helix 会变成 Wavetable），效果器参数的自动化和插入轨之间的路由会被丢弃。见[从 Ableton Live 转来](/wiki/coming-from-ableton/) |
| `.dawproject` | `文件 › 打开...` | `文件 › 导出 › DAWproject (.dawproject)...` | Bitwig、Studio One、Cubase 等程序使用的开放交换格式：轨道、文件夹、片段、音符、带形变标记的音频、自动化、速度、标记、带发送的混音台，以及带状态的 VST3、VST2 和 CLAP 插件。导入时，音符效果器和 MIDI 表情会被舍去；导出时，片段网格、其他编排和部分逐音符的细节会被舍去。两个方向都会列出被舍去的内容 |
| `.band` | `文件 › 打开...`（工程包、其中的 `projectData` 文件或它的 zip） | 不支持 | GarageBand 10 或 GarageBand for iOS 工程，作为简易工作流程的音轨。部分导入：乐器音轨用内置合成器和鼓来播放，GarageBand for iOS 中制作的 Sampler 乐器除外。GarageBand 11 及更早版本的工程无法读取 |
| `.blx` | `文件 › 打开...`，或用简易工作流程的 `文件夹...` 导入其文件夹 | 不支持 | 由 Cakewalk 或 Sonar 下载的 BandLab 工程，作为简易工作流程的音轨，带区域、速度、调性、混音、音量和声像自动化以及常用效果器。部分导入：BandLab 的乐器音色和其他踏板效果会列在导入报告中 |
| `.mid`、`.midi` | `文件 › 打开...` 会新建一个工程。`文件 › 导入 › MIDI文件...` 和钢琴卷帘则导入到当前工程 | `文件 › 导出 › MIDI文件...`（`Ctrl+Shift+M`） | |
| `.mod`, `.xm`, `.s3m`, `.it` | `文件 › 打开...` | 不支持 | Tracker 模块（ProTracker、FastTracker 2、Scream Tracker 3、Impulse Tracker）。乐器会变成带有采样的采样器通道，乐曲会连同速度变化变成模式片段，`视图 › 跟踪器`可以编辑音符。部分支持：滑音、颤音和琶音等效果、包络以及正在发声的音符上的音量变化会列在导入报告中 |
| `.wav` | 作为采样或音频片段 | `文件 › 导出 › Wave文件...`，16 位、24 位或 32 位浮点 | 可选抖动、速度信息、循环标记和播放列表标记 |
| `.mp3` | 作为采样或音频片段 | `文件 › 导出 › MP3文件...` | 恒定码率 |
| `.ogg` | 作为采样或音频片段 | `文件 › 导出 › OGG文件...` | |
| `.flac` | 作为采样或音频片段 | `文件 › 导出 › FLAC文件...`，压缩级别 0 到 8 | 每个级别都是无损的 |
| `.aif`、`.aiff` | 作为采样或音频片段 | `文件 › 导出 › AIFF文件...`，16 位或 24 位 | |
| `.m4a` | – | `文件 › 导出 › M4A文件...` | 仅导出。AAC，在 Windows 和 macOS 上使用系统的编码器；Linux 上不提供 |
| `.wv` | 作为采样 | 不支持 | WavPack 采样，与 FL Studio 的包一起提供 |
| `.sf2`、`.sf3` | 由 LYNX Soundfont 播放 | 不支持 | SoundFont |
| `.dwp`、`.dwb` | 在浏览器中双击、拖放到通道机架，或使用 DirectWave 的 `打开程序 / 音色库...` | DirectWave 的程序和音色库保存 | DirectWave 程序和音色库。采样会在 DirectWave 查找它们的位置查找 |
| `.gig` | 作为 DirectWave 通道 | 不支持 | GigaStudio 乐器：力度层、轮流采样、循环、调音和放大包络。释放触发采样、键位切换和压缩采样暂不读取 |
| `.ncw` | 作为采样或音频片段 | 不支持 | Kontakt 的压缩采样。Kontakt 的 `.nki` 乐器需要把 Kontakt 本身作为插件；受保护的音色库永远不会被解密 |
| `.fst` | 从浏览器 | 不支持 | FL Studio 原生插件的预设。仅限 LYNX SHARE 有对应引擎的插件 |
| `.fsc` | 钢琴卷帘和事件编辑器 | 钢琴卷帘和事件编辑器 | FL Studio 乐谱和自动化文件 |
| `.lynxpreset`、`.fxp`、`.fxb`、`.vstpreset` | 宿主插件预设菜单中的 `加载预设文件...` | `保存预设...` 和 `导出预设文件...` | 第三方插件预设。`.fxp` 和 `.fxb` 用于 VST2 插件，`.vstpreset` 用于 VST3 插件，并且只在插件于 LYNX SHARE 内部运行时可用 |
| 视频文件 | `文件 › 导入 › 视频文件...` 会把视频放到播放列表上 | 导出窗口中的 `同时写出带此混音的视频` | 导入在 Windows 和 macOS 上使用系统解码器，在 Linux 上需要 FFmpeg。导出需要 FFmpeg |

## 其他导出

| 命令 | 作用 |
|---|---|
| 导出窗口的 `文件` 选项 | 分轨：每个混音台插入轨、每个通道一个文件，或每个通道一个文件且仅含乐器 |
| `文件 › 导出 › 所有 Playlist 轨道` | 每条播放列表轨道一个文件 |
| `文件 › 导出 › 所有 Playlist 轨道组` | 每个轨道组一个文件 |
| `文件 › 导出 › 母带处理...` | 渲染工程，并在你的电脑上把它母带处理到响度目标 |
| `文件 › 导出 › 用于其他应用...` | 一个文件夹，内含分轨、MIDI 文件、Ableton Live 套装、FL Studio 工程、完整混音和 README，可按需打包成 zip |
| `文件 › 导出 › 项目骨架...` | 把通道和效果器的预设及评分写入一个文件夹 |

导出窗口可以渲染整首歌、当前样式或播放列表选区。

## 从命令行

在 Windows 上，程序是 `LYNX.exe`。在 macOS 和 Linux 上是 `LYNX`。

```
LYNX.exe project.lynx                       open a project (any file File › Open takes)
LYNX.exe --export project.flp out.flac      render the song (wav, flac, ogg, mp3, aif; m4a on Windows and macOS)
LYNX.exe --stems project.flp base.wav       one WAV per mixer insert that has sound
LYNX.exe --export-flp project.lynx out.flp  write an FL Studio project
```

`--export`、`--stems` 和 `--export-flp` 也接受 `.als` 作为输入。

## 须知

- 超过 30 秒的音频文件会从磁盘缓存中流式读取。
- 保存时永远不会写入 `.flp`、`.als`、`.flm`、`.dawproject`、`.band` 或 `.blx`。导出命令会写出新文件。
- `.wv` 文件是只读的。
- `.zip` 会先解压到一个文件夹，再打开其中的工程。

## 相关页面

- [导入工程](/wiki/importing-projects/)
- [导出](/wiki/exporting/)
- [命令行](/wiki/command-line/)
- [从 FL Studio 转来](/wiki/coming-from-fl-studio/)
- [从 Ableton Live 转来](/wiki/coming-from-ableton/)
