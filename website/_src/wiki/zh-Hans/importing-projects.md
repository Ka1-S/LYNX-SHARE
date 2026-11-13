---
source: 358890815a
---
`文件 › 打开...`（`Ctrl+O`）可以打开 LYNX SHARE 自己的 `.lynx` 工程，也可以打开 `.flp`、`.als`、`.flm` 和 `.mid` 文件。来自其他程序的工程属于导入：它会成为一个新的、未保存的 LYNX SHARE 工程，原文件绝不会被写入。

## 打开来自其他程序的工程

1. 选择 `文件 › 打开...`，然后选取 `.flp`、`.als`、`.flm` 或 `.mid` 文件。
2. 等待 LYNX SHARE 查找工程的插件和采样。
3. 如果有内容没有找到，会打开 `缺失的文件` 窗口。请把它指向你的采样，或者关闭它。
4. 标题栏现在会显示工程的来源，例如“（从 FL Studio 导入）”。
5. 选择 `文件 › 保存`。LYNX SHARE 会询问把 `.lynx` 文件写到哪里。

## 能带过来什么

| 格式 | 会转换 | 不会转换 |
|---|---|---|
| `.flp`（FL Studio） | 通道、采样、样式、滑音和滑奏音符、所有编排、标记、拍号变化、自动化、控制器关联、带路由的混音台、侧链、轨道 EQ 和原生效果器 | FLEX 包是加密的，永远不会被打开。FLEX 通道会得到一个按其预设名称选定的合成器音色 |
| `.als`（Live 11 和 12） | MIDI 轨道、音频轨道、编组轨道和返回轨道，速度、拍号、定位点、循环，音量、声像、静音、独奏和发送及其自动化，会话片段和场景，扭曲标记，机架和 Drum Rack，以及 Live 自己的许多设备（EQ Eight、Compressor、Glue Compressor、Reverb、Delay、Auto Filter、Saturator、Utility、Simpler、Drift 等），作为 LYNX SHARE 自己的设备 | Live 中没有 LYNX SHARE 对应项的设备（Hybrid Reverb、Grain Delay、Amp、Roar、FM Operator 音色等）会被丢弃并列出。MIDI Effect Rack 不会被转换 |
| `.flm`（FL Studio Mobile） | 速度、拍号、每个通道一个混音插入轨、每条泳道一条播放列表轨道、样式、带偏移和音高的音频片段、鼓组、DW Sampler 和 `.dwp` 程序、Slicer、MiniSynth、Transistor Bass、3x Osc、GMSynth 和 SuperSaw、效果器、自动化和和弦集 | FLM 的其他发生器改用内置合成器播放 |
| `.mid` | 音符、拍号和所有速度变化 | MIDI 文件不含声音：通道会得到 LYNX SHARE 合成器或一个 MIDI Out 通道 |

`.flp` 中的第三方 VST2、VST3 和 CLAP 插件，以及 `.als` 中的 VST2 和 VST3 插件，如果你的电脑上安装了相同的插件，就会带着保存的状态加载。

## FL Studio 自己的插件

FL Studio 的发生器和效果器不会被加载。LYNX SHARE 读取它们保存的设置，并在自己重建的引擎上播放。每个引擎都有一个质量等级，从 `Exact`（在所有测试过的状态下都与原版逐位一致）到 `Placeholder`（设置会保留，但插件静音或让音频直通）。各等级的含义见[从 FL Studio 转来](/wiki/coming-from-fl-studio/)。

没有安装 FL Studio 也能打开 `.flp`。有些引擎需要波形、采样或脉冲响应，它们会在运行时从你自己的 FL Studio 安装中读取。

## 缺失的采样

![缺失的文件窗口的采样标签页：找到的文件为绿色，缺失的为琥珀色，带有 Search folder、Locate 和 Skip](/assets/img/wiki/missing-files.webp)

`采样` 标签页列出每个未找到的采样，以及它保存的路径和使用它的对象。

| 命令 | 作用 |
|---|---|
| `搜索文件夹...` | 在文件夹及其子文件夹中搜索所有缺失的采样 |
| `定位...` | 询问某一个采样在哪里。在同一文件夹中找到的其他采样也会一并采用 |
| `跳过` | 略过该采样。它的通道保持静默 |

`应用` 会使用已找到的文件。

`自动找到` 标签页列出 LYNX SHARE 自己在其他文件夹中找到的文件。请保存工程，以保留这些新位置。

## 缺失的插件

LYNX SHARE 按标识符查找每个第三方插件，然后按文件，再按名称。以 VST2 保存的工程可以用同一产品的 VST3 打开，反过来也一样，但此时使用默认设置。

当某个效果器没有安装时，一个作用相同的 LYNX SHARE 效果器会占据这个插槽：混响换混响，压缩器换压缩器。

- 插槽显示原插件的名称，并带有 LYNX SHARE 徽标。
- 如果能读取保存的状态，就会转换设置。否则替代插件从默认预设开始。产品、它们的替代插件，以及每个产品转换哪些内容的对照表，见 [PLUGIN_FALLBACK.md](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/PLUGIN_FALLBACK.md)。
- 原插件参数的自动化会保留，但不会驱动替代插件。
- 原插件的引用和状态仍保留在工程中。装上该插件后，工程会再次用它打开。
- 替代插件插槽右键菜单中的 `旁通此工程的所有LYNX SHARE替代` 可一次性关闭所有替代插件。

开关是 `选项 › 常规设置...` 中的 `用LYNX SHARE对应插件替代缺失的插件`，默认开启。关闭后，缺失效果器的插槽会从混音台中略去。缺失的乐器不做替代。它会变成一个内置合成器，预设按通道名称选定。

要找回真正的插件，请先安装它，选择 `选项 › 扫描插件`，然后重新打开工程。

## MIDI 文件

![导入 MIDI 数据窗口：轨道、通道、通道类型和选项](/assets/img/wiki/midi-import-dialog.webp)

`文件 › 打开...` 会从 MIDI 文件创建新工程。`文件 › 导入 › MIDI文件...` 则先打开 `导入MIDI数据` 窗口，让你选择导入什么。

| 设置 | 作用 |
|---|---|
| `要导入哪些轨道？` | 所有轨道，或其中一条 |
| `要导入哪些通道？` | 单击某个 MIDI 通道可将其略去。右键单击则只保留该通道 |
| `通道类型` | `LYNX SHARE合成器` 或 `MIDI Out` |
| `开始新工程` | 开始一个空白工程。关闭：添加到已打开的工程中 |
| `每个轨道创建一个通道` | 关闭：每个 MIDI 通道一个通道 |
| `为新通道设置混音轨道` | 把新通道路由到连续的混音插入轨 |
| `导入拍号` | 文件的拍号成为工程的拍号 |

在[钢琴卷帘](/wiki/piano-roll/)中按 `Ctrl+M` 可把 MIDI 文件导入当前通道。

## 须知

- `帮助 › 缺失的文件...` 可以在之后再次打开该窗口。`帮助 › Last FL Studio import report` 列出哪些内容被近似还原、被略去或未安装。
- Live 套装的会话片段会进入片段启动布局的片段网格。`选项 › 常规设置...` 中的 `Live 工程的会话片段` 则会改为把它们放在播放列表上。

## 相关

- [文件格式](/wiki/file-formats/)
- [从 FL Studio 转来](/wiki/coming-from-fl-studio/)
- [从 Ableton Live 转来](/wiki/coming-from-ableton/)
- [插件](/wiki/plugins/)
- [导出](/wiki/exporting/)
