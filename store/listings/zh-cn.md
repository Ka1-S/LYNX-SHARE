# Store listing — 简体中文 (zh-cn)

<!-- Format: see en-us.md. Simplified Chinese; Partner Center may list it as zh-hans / zh-cn. -->

## Title

LYNX SHARE

## Short description

免费开源的DAW，一个程序提供三种工作流程：FL Studio风格的模式（Channel Rack、Piano Roll、Playlist、Mixer）、Ableton Live风格的片段启动，以及BandLab和FL Studio Mobile风格的简易单窗口模式。可打开.flp、.als、.flm、.dawproject、GarageBand的.band和BandLab的.blx工程，并可导出.flp、.als、.dawproject和.flm。

## Description

LYNX SHARE是一款免费开源的数字音频工作站，用来制作节拍和歌曲。它在一个程序里提供三种工作流程，可随时切换，不会改变工程：

• FL Studio风格的模式（默认）：带步进音序器的Channel Rack、模式、Piano Roll、带编曲的Playlist，以及带插入、发送和侧链的Mixer。
• Ableton Live风格的片段启动：片段和场景的网格、会话混音器，以及片段视图和设备视图。
• 简易工作流程，把BandLab易上手的单窗口风格和FL Studio Mobile的工作方式结合在一起：音轨和时间线、循环、打击垫和快速录音，还有引导式的第一首歌。

首次启动会询问你来自哪里，并打开相应的布局。

它在任何PC上都能保持轻快。

• 轻量：无锁音频引擎将通道和混音轨分配到所有 CPU 核心，自动跳过空闲通道，长音频直接从硬盘流式读取。
• 齐全：100 多款内置乐器和效果器——合成器（波表、FM，以及LYNX Model的敲击乐器物理模型）、鼓机、采样器、均衡器、压缩器、限幅器、混响、延迟、音高校正、可视化工具等等——外加可作用于任何旋钮的调制器（LFO、包络跟随器、步进音序器等）。
• 你的插件：支持 VST3、CLAP 和 VST2 乐器与效果器，每个插件都可选择在独立的沙箱进程中运行，插件崩溃也不会拖垮整个工程。
• 你的工程：打开FL Studio工程（.flp），包括模式、音符、混音器、自动化和采样；打开并保存Ableton Live 11/12套装（.als），包括Session片段；打开并保存DAWproject文件（.dawproject），可用于Bitwig、Studio One、Cubase等应用；可导出.flp回到FL Studio，也可为FL Studio Mobile保存.flm副本；可导入FL Studio Mobile工程（.flm，部分支持）、GarageBand工程（.band，部分支持）、BandLab工程（.blx，部分支持）和MIDI文件。
• Tracker 模块：.mod、.xm、.s3m 和 .it 文件连同采样、模式和速度一起打开，内置的跟踪器可以编辑音符。
• 更快地编排：用于整段歌曲部分的段落轨、自带音符的轨道片段、可隐藏或停用轨道以节省 CPU、能找到每个操作的命令搜索，以及并排打开两个工程并可在其间复制粘贴。
• 一起做音乐：把工程作为一个文件发出，或与最多三位朋友实时编辑，端到端加密。
• 音频编辑：Melodyne等ARA 2插件可直接在音频片段上工作，复音音符编辑器可移动和重新调音和弦中的单个音符，音频还可转换为MIDI。
• 低延迟：支持 ASIO（包括适用于任何设备的 LYNX SHARE 自带 ASIO 驱动）以及 WASAPI 共享和独占模式。
• 录制音频和 MIDI（也支持 MPE）、编辑音频、自动化任意参数，导出为 WAV、FLAC、MP3、OGG、AIFF、M4A 或分轨（stems）。
• 离线可用：无需账号，在你同意之前不会联网。
• 界面语言：简体中文、繁体中文、英语、葡萄牙语、西班牙语、法语、德语、日语、韩语等 15 种语言。

无需账号，没有订阅，没有广告，也没有遥测。LYNX SHARE 是自由软件（GNU AGPL-3.0），源代码托管在 GitHub 上。

LYNX SHARE是独立项目，与Image-Line或Ableton无关联，也未获其认可。FL Studio和FL Studio Mobile是Image-Line Software的商标；Ableton和Live是Ableton AG的商标。其他产品名称是其各自所有者的商标。

LYNX SHARE 目前处于 Alpha 阶段：难免有不完善之处，如遇问题请在 GitHub 上反馈。

## What's new

LYNX SHARE 0.1.31.2 修复了起始页崩溃的一个可能原因、停靠的 VST3 插件上方的“LYNX 优先”键盘设置、只循环第一小节的拉长片段、由 FL 工程生成的自动化轨道、FL 拒绝打开的导出 .flp、浏览器中 FL 自带合成器的预设，以及几个较小的问题。

LYNX SHARE 0.1.31.1 修复了你们在 0.1.31 中反馈的问题：控件上的 FL 工程自动化、不再打开的插件窗口、Device 视图、文件名带文件夹的导出、乐器轨道上的片段、提示栏中的崩溃，以及启动时的内存。

LYNX SHARE 0.1.31：内置跟踪器，还能打开跟踪器模块（.mod、.xm、.s3m、.it）；LYNX Model，马林巴、颤音琴、木琴、钟琴、管钟和卡林巴的物理模型；JSFX 效果器和乐器脚本；会设置合适布局的欢迎卡片，以及带最近工程的起始页；可找到每条命令的搜索；并排的两个工程，之间可复制粘贴；段落轨、带Smart Tempo的和弦轨和轨道版本；自动化写入模式、捕获自动化和适用于任意旋钮的调制器；FL Studio Mobile .flm导出；片段启动工作流程中内嵌显示的Live机架设备，带Macro Map和Solo / Cue；简易工作流程中的闪避于和回声发送；混音插入轨上的ARA插件；播放效果更接近FL的FL Studio工程；并修复了约30个已报告的问题。

## Features

1. 一个程序，三种工作流程：默认是FL Studio风格的模式，或Ableton Live风格的片段启动，或BandLab和FL Studio Mobile风格的简易工作流程
2. 打开FL Studio（.flp及压缩包）、Ableton Live（.als）、FL Studio Mobile（.flm）、DAWproject、GarageBand和BandLab工程以及MIDI文件；保存为.flp、.als、.dawproject和.flm副本
3. 100 多款内置乐器和效果器
4. 支持 VST3、CLAP 和 VST2 插件，可选在独立进程中沙箱运行
5. 低延迟音频：ASIO、WASAPI 共享和独占模式
6. 多核引擎，老电脑也能轻松运行
7. 音频和 MIDI 录音、音频编辑器、自动化片段
8. 导出 WAV、FLAC、MP3、OGG、AIFF、M4A 和分轨
9. 支持简体中文等 15 种界面语言
10. 免费开源（AGPL-3.0）：无需账号，无广告，无遥测
11. 支持托管ARA 2插件、复音音符编辑器和音频转MIDI
12. 打开 Tracker 模块（.mod、.xm、.s3m、.it），并在内置跟踪器中编辑音符
13. LYNX Model：马林巴、颤音琴、木琴、钟琴、管钟和卡林巴的物理模型
14. 可作用于任何旋钮的调制器：LFO、包络跟随器、ADSR、步进、随机、宏、按键跟踪和力度
15. 段落轨、隐藏和停用轨道，以及能找到每个命令的搜索
16. 在线会话：与最多三位朋友实时编辑工程，端到端加密
17. 离线可用：无需账号，在你允许之前不会联网

## Search terms

- DAW
- 编曲软件
- 音乐制作
- 做beat
- 音频编辑
- MIDI音序器
- VST宿主

## Screenshot captions

- 01-workspace.png — LYNX SHARE 工作区：浏览器、Playlist、Piano Roll、Channel Rack 和 Mixer
- 02-playlist.png — 在 Playlist 中编排 Pattern、音频和自动化
- 03-piano-roll.png — 在 Piano Roll 中编写旋律与和弦
- 04-mixer.png — 用插入、发送、侧链和内置效果器混音
- 05-instruments-effects.png — 步进音序器、内置合成器和参数均衡器
- 06-audio-editing.png — 在复音音符编辑器中逐个编辑和弦里的音符，或在Melodyne等ARA 2插件中打开音频片段
- 07-chord-helper.png — 和弦助手为选中的和弦推荐相关调性和借用和弦
- 08-drum-machine.png — 在简易工作流程的BandLab风格鼓机中编排节拍
- 09-helix-plus.png — 波表合成器Helix与Helix+：第三个振荡器、颗粒与频谱层、8个宏、可绘制的LFO和效果器
- 10-song-lanes-tracker.png — 用于歌曲段落与和弦的段落轨和和弦轨、内置的跟踪器（Tracker），以及可驱动任意旋钮的调制器
