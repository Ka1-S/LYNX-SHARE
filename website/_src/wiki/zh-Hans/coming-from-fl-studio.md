---
source: 3bf7e0c0ab
---
LYNX SHARE 保留了你熟悉的工作方式：通道机架、样式、播放列表和混音台，快捷键也一样。它可以直接打开 `.flp` 工程，也可以写回一个。

LYNX SHARE 是一个独立的程序。它与 Image-Line 没有任何关联，未获其认可或赞助，也不包含任何 Image-Line 的代码。FL Studio 是 Image-Line Software 的注册商标。

## 打开 .flp 工程

1. 选择 `文件 › 打开...`（`Ctrl+O`），选取该 `.flp`。
2. LYNX SHARE 会解析工程中的插件和采样，然后打开工程。标题会显示“(imported from FL Studio)”。
3. 选择 `帮助 › Last FL Studio import report`，查看哪些内容被近似处理、被舍去或未找到。
4. 选择 `帮助 › 缺失的文件...`，查看缺失的采样和插件。
5. 选择 `文件 › 保存`。它会询问把 `.lynx` 文件写到哪里。`.flp` 本身永远不会被写入。

## 对应关系

| 在 FL Studio 中 | 在 LYNX SHARE 中 |
|---|---|
| 播放列表、通道机架、钢琴卷帘、混音台 | 名称相同，快捷键为 `F5`、`F6`、`F7` 和 `F9` |
| 插件选择器 | `F8` |
| 浏览器 | `Alt+F8` |
| 样式 / 歌曲模式 | `L` |
| 撤销 | `Ctrl+Z` 撤销，撤销之后再按则重做。`Ctrl+Alt+Z` 向后撤销 |
| 保存新版本 | `Ctrl+Alt+S` |
| 导出 | `Ctrl+R`，MP3 为 `Ctrl+Shift+R`，MIDI 为 `Ctrl+Shift+M` |
| 混音台插入轨、发送和侧链 | 相同，每个插入轨有 10 个效果器插槽 |
| Fruity Wrapper | 第三方插件直接托管，见[第三方插件](/wiki/plugins/) |
| 原生生成器和效果器 | 在 LYNX SHARE 重建的引擎上播放，见下文 |
| `.flp` 工程文件 | `.lynx` 工程文件 |

`帮助 › 快捷键` 列出了所有按键。

## 会导入什么

- 通道、采样、样式、滑音（slide）和连音（porta）音符、所有编排、标记、拍号变化、自动化和控制器关联。
- 混音台：路由、电平、侧链、轨道 EQ，以及带有已保存设置的原生效果器。
- Fruity Wrapper 中的第三方 VST3、VST2 和 CLAP 插件：用你电脑上安装的同一个插件替换，并带上其已保存的状态。

## FL Studio 原生插件

FL Studio 自己的生成器和效果器不会被加载。LYNX SHARE 用它重建的引擎来播放它们，每个引擎都有一个质量等级：

| 等级 | 含义 |
|---|---|
| 精确 | 在所有测试过的状态下与原版逐位相同 |
| 近乎精确 | 在大多数状态下逐位相同，其余状态的差异低于约 -100 dB |
| 接近 | 算法相同，在几乎所有测试过的状态下与原版相差不超过 -60 dB |
| 近似 | 已保存的设置被映射到 LYNX SHARE 内置的合成器或效果器上 |
| 占位 | 保留状态，插件静音或让音频直通 |

每个引擎的等级，以及各自还缺少什么，见 [PLUGINS.md](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/PLUGINS.md)。另见[内置乐器和效果器](/wiki/built-in/)。

## 哪些内容需要你自己安装的 FL Studio

打开 `.flp` 不需要安装 FL Studio。在同一台电脑上装有 FL Studio 则会多出两样东西：

- FL Studio 的出厂采样和它的插件数据库，这样无需扫描就能找到采样和插件。
- 某些引擎需要的内置波形、采样、脉冲响应和表格（FL Keys、Soundgoodizer、Maximus，以及 PLUGINS.md 中标有剑号的其他引擎）。它们在运行时从你的安装中读取，不随 LYNX SHARE 一起提供。

在 Linux 上，LYNX SHARE 会在 Wine 下查找 Windows 版 FL Studio。在 macOS 上会查找应用包，但这一点未经测试。

## 导出回 .flp

1. 选择 `文件 › 导出 › FL Studio 工程...`。
2. 选择 `FL Studio 工程 (.flp)` 或 `压缩循环包 (.zip，含全部采样)`。
3. 选择 FL Studio 没有的内容如何处理：渲染为音频（声音相同），或使用最接近的 FL 插件（仍可编辑，声音可能不同，并且仅 LYNX 有的乐器会被舍去）。

该文件是 FL Studio 2026 工程。采样器、音频片段、FL 自己的生成器和效果器、第三方插件（在 Fruity Wrapper 中）、样式、播放列表、混音台和自动化都会连同设置一起转过去。报告会列出哪些内容被精确转出，哪些被渲染、替换或舍去。

## FL Studio Mobile (.flm)

`文件 › 打开...` 也能打开 `.flm` 工程。导入是部分的。

- 会导入：速度、拍号、每个 FLM 通道对应一个混音台插入轨、每条泳道对应一个播放列表轨道、样式（每次循环作为一个片段）、带偏移和音高的音频片段、鼓组、DW Sampler 采样和 `.dwp` 程序以及 Slicer（用 FLM 自己的引擎）、MiniSynth、Transistor Bass、3x Osc、GMSynth 和 SuperSaw 乐器、FLM 的效果器（作为 Pocket 效果器）、电平、声像、静音、独奏、弯音、发送和乐器参数的自动化，以及和弦集和调锁定。
- 不会导入：FLM 的其他乐器，它们用内置合成器播放。
- 采样会在 `.flm` 旁边以及 FL Studio 中 FLM 资源的副本里查找。找不到的 `.dwp` 会用替身来播放。

## 须知

- FLEX 包是加密的，永远不会被打开。FLEX 通道会根据其预设名称得到一个合成器音色。
- 调制器会被 `.flp` 导出舍去，导出报告里会写明这一点。
- 导入报告会列出：哪些内容由内置合成器近似处理，哪些没有对应项，哪些插件被找到或未安装，哪些效果器被舍去或只部分重现，以及缺失了多少个采样。
- 未安装的第三方效果器会被作用相同的 LYNX SHARE 效果器替换。原来的效果器仍保留在工程中。

## 相关页面

- [内置乐器和效果器](/wiki/built-in/)
- [第三方插件](/wiki/plugins/)
- [文件格式](/wiki/file-formats/)
- [快捷键](/wiki/shortcuts/)
- [只有 LYNX SHARE 才能对 FL 设置做的事](/why/#fl-settings)
