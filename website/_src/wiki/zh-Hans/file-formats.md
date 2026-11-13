---
source: a79e43f031
---
LYNX SHARE 把工程保存为 `.lynx` 文件。它也能打开其他程序的工程，加载常见的音频文件，并导出供其他程序使用的音频、MIDI 和工程。下表列出了 0.1.26 alpha 的所有格式。

从其他程序的格式打开的工程属于导入。此时 `文件 › 保存` 会询问把 `.lynx` 文件写到哪里，原始文件永远不会被改动。

## 打开其他程序的工程

1. 选择 `文件 › 打开...`（`Ctrl+O`）。
2. 选取 `.flp`、`.als`、`.flm`、`.mid` 或 `.zip` 文件。
3. 对于 `.flp`，选择 `帮助 › Last FL Studio import report`，查看哪些内容被近似处理或舍去。
4. 选择 `文件 › 保存`，并选择 `.lynx` 文件的写入位置。

## 格式

| 格式 | 打开 | 保存 / 导出 | 说明 |
|---|---|---|---|
| `.lynx` | `文件 › 打开...` | `文件 › 保存`，带自动保存 | LYNX SHARE 自己的工程格式（JSON） |
| `.zip` | `文件 › 打开...` 把它解压到一个文件夹，然后打开其中的工程 | `文件 › 导出 › 工程及采样（.zip）...` | 也能打开 FL Studio 的压缩循环包 |
| `.lynxpack` | 显示摘要后打开。其中的一部分会合并到宿主的工程 | `文件 › 在线会话 › 发送工程...` | 在[在线会话](/wiki/online-sessions/)中发送的工程或其中一部分。第三方插件的部分以音频渲染加插件设置的形式传送 |
| `.flp` | `文件 › 打开...` | `文件 › 导出 › FL Studio 工程...`，即 FL Studio 2026 工程或压缩循环包 | 原生插件在重建的引擎上播放，精确程度各不相同。FLEX 包永远不会被打开。导出时，仅 LYNX 有的乐器和效果器会被渲染为音频，或换成最接近的 FL 插件，调制器则会被舍去。见[从 FL Studio 转来](/wiki/coming-from-fl-studio/) |
| `.flm` | `文件 › 打开...` | 不支持 | FL Studio Mobile 工程。部分导入：除 MiniSynth、Transistor Bass、3x Osc、GMSynth、SuperSaw、DW Sampler、Slicer 和 DirectWave 之外的乐器用内置合成器播放 |
| `.als` | `文件 › 打开...`，Live 11 和 12 的套装 | `文件 › 导出 › Ableton Live 套装 (.als)...`，适用于 Live 12 或 Live 11 | 导入时，Live 中没有 LYNX SHARE 对应项的设备会被丢弃。导出时，LYNX SHARE 自己的乐器和效果器不会被转换，效果器参数的自动化和插入轨之间的路由会被丢弃。见[从 Ableton Live 转来](/wiki/coming-from-ableton/) |
| `.mid`、`.midi` | `文件 › 打开...` 会新建一个工程。`文件 › 导入 › MIDI文件...` 和钢琴卷帘则导入到当前工程 | `文件 › 导出 › MIDI文件...`（`Ctrl+Shift+M`） | |
| `.wav` | 作为采样或音频片段 | `文件 › 导出 › Wave文件...`，16 位、24 位或 32 位浮点 | 可选抖动、速度信息、循环标记和播放列表标记 |
| `.mp3` | 作为采样或音频片段 | `文件 › 导出 › MP3文件...` | 恒定码率 |
| `.ogg` | 作为采样或音频片段 | `文件 › 导出 › OGG文件...` | |
| `.flac` | 作为采样或音频片段 | `文件 › 导出 › FLAC文件...`，压缩级别 0 到 8 | 每个级别都是无损的 |
| `.aif`、`.aiff` | 作为采样或音频片段 | 不支持 | |
| `.wv` | 作为采样 | 不支持 | WavPack 采样，与 FL Studio 的包一起提供 |
| `.sf2`、`.sf3` | 由 LYNX Soundfont 播放 | 不支持 | SoundFont |
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
| `文件 › 导出 › 项目骨架...` | 把通道和效果器的预设及评分写入一个文件夹 |

导出窗口可以渲染整首歌、当前样式或播放列表选区。

## 从命令行

在 Windows 上，程序是 `LYNX.exe`。在 macOS 和 Linux 上是 `LYNX`。

```
LYNX.exe project.lynx                       open a project (.lynx, .flp, .als, .flm or .mid)
LYNX.exe --export project.flp out.flac      render the song (wav, flac or ogg)
LYNX.exe --stems project.flp base.wav       one WAV per mixer insert that has sound
LYNX.exe --export-flp project.lynx out.flp  write an FL Studio project
```

`--export`、`--stems` 和 `--export-flp` 也接受 `.als` 作为输入。

## 须知

- 超过 30 秒的音频文件会从磁盘缓存中流式读取。
- 保存时永远不会写入 `.flp`、`.als` 或 `.flm`。导出命令会写出新文件。
- `.wv` 文件是只读的。
- `.zip` 会先解压到一个文件夹，再打开其中的工程。

## 相关页面

- [导入工程](/wiki/importing-projects/)
- [导出](/wiki/exporting/)
- [命令行](/wiki/command-line/)
- [从 FL Studio 转来](/wiki/coming-from-fl-studio/)
- [从 Ableton Live 转来](/wiki/coming-from-ableton/)
