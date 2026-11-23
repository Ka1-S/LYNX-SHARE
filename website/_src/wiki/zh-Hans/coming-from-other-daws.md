---
source: c82c79e958
---
LYNX SHARE 有三种布局。如果你从 Logic、Cubase、Reaper、Pro Tools 或 Studio One 转过来，请从 **带着混音台的简易布局** 开始：时间线上每个乐器或声音一条音轨，和你熟悉的一样。想用样式时，还有模式布局，也就是 FL Studio 的工作方式。

## 你的第一步

1. **选择布局。** 第一次启动 LYNX SHARE 时，欢迎卡片会问你从哪里转过来。点击 `Logic、Cubase、Reaper、Pro Tools 或 Studio One`：简易布局会打开它的新音轨卡片，混音台会带着你的第一条音轨出现。之后，`视图 › 布局样式 › 简易工作流程` 可随时切换到它，`F9` 可打开混音台。
2. **添加音轨。** `+ 添加音轨` 提供 `人声 / 音频`、`乐器`、`鼓`、`采样器`、`吉他 / 贝斯`、`导入` 和 `循环素材`。用电脑按键、屏幕键盘或 MIDI 键盘来演奏乐器音轨。
3. **录音。** 在 `选项 › 音频设置...` 中选择你的音频设备，然后见[录下你的声音](/learn/record-voice/)。

## 哪些相同

- 时间线上的音轨，带有音频、MIDI 和自动化片段。
- 音频和 MIDI 录音，支持循环录音和合成录音。
- 带有插入效果、发送、侧链、编组和延迟补偿的混音台。
- 第三方插件：所有系统上的 VST3 和 CLAP，macOS 上的 Audio Units，Linux 上的 LV2，Windows 和 Linux 上的 VST2。
- 导出歌曲、按音轨导出分轨，以及导出 MIDI。

## 哪些不同

- **音轨和混音台插入轨是分开的。** 在简易布局中，每条新音轨都有自己的通道和混音台轨道。在模式布局中，你要像在 FL Studio 里一样自己把通道路由到插入轨。
- **样式。** 在模式布局中，音符放在样式里，你把样式摆到播放列表上，一个样式可以包含多个乐器。
- **按键沿用 FL Studio。** `Ctrl+Z` 撤销一步，再按一次则重做。`Ctrl+Alt+Z` 可以撤销得更远。
- **不能导入 Logic、Cubase、Reaper 或 Pro Tools 的工程。** 请用 DAWproject 文件、分轨或 MIDI 来迁移工程（见下文）。

## 按键

| 要做什么 | 按键 |
|---|---|
| 播放，或停止并返回 | `Space` |
| 开启或关闭录音 | `R` |
| 混音台 | `F9` |
| 钢琴卷帘 | `F7` |
| 播放列表 | `F5` |
| 浏览器 | `Alt+F8` |
| 打开、保存 | `Ctrl+O`、`Ctrl+S` |
| 撤销，以及撤销得更远 | `Ctrl+Z`、`Ctrl+Alt+Z` |
| 导出音频、MP3、MIDI | `Ctrl+R`、`Ctrl+Shift+R`、`Ctrl+Shift+M` |

`帮助 › 快捷键` 列出了所有按键。见[快捷键](/wiki/shortcuts/)。

## 迁移工程

- **Cubase、Studio One 和 Bitwig**：在那边保存一个 DAWproject 文件（`.dawproject`），然后用 `文件 › 打开...` 打开。音轨、文件夹、片段、音符、带扭曲标记的音频、自动化、速度、标记、带发送的混音台，以及带状态的 VST3、VST2 和 CLAP 插件都会带过来。`文件 › 导出 › DAWproject (.dawproject)...` 则走相反的方向。
- **Logic、Reaper 和 Pro Tools**：把每条音轨从歌曲开头导出为音频或 MIDI 文件，放进同一个文件夹。在 LYNX SHARE 中点击 `+ 添加音轨`，再点击 `导入` 旁边的 `文件夹...`：每个文件会成为一条从开头开始的音轨。
- **导出回去**：导出窗口会为每个混音台插入轨或每个通道写出一个文件，`文件 › 导出 › MIDI文件...` 则写出音符。

见[文件格式](/wiki/file-formats/)和[导出](/wiki/exporting/)。

## 须知

- Logic 是 Apple Inc. 的商标，Cubase 是 Steinberg Media Technologies 的商标，Reaper 是 Cockos Incorporated 的商标，Pro Tools 是 Avid Technology 的商标，Studio One 是 PreSonus Audio Electronics 的商标，Bitwig 是 Bitwig GmbH 的商标。LYNX SHARE 是一个独立的程序，与它们没有关联，也未获其认可。
- 片段启动布局会在时间线旁边增加一个带场景的片段网格。见[布局](/wiki/layouts/)。

## 相关页面

- [布局](/wiki/layouts/)
- [混音台](/wiki/mixer/)
- [录音](/wiki/recording/)
- [从 FL Studio 转过来](/wiki/coming-from-fl-studio/)
- [学习：教程](/learn/)
