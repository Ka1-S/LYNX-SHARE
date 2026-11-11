---
source: a5c1d056c8
---
LYNX SHARE 也可以不打开窗口直接运行。一个选项可以把工程渲染为音频，另外一些可以把它转换为 FL Studio 工程或 Ableton Live 套装，还有几个用于测试。工程自身的检查也使用同样的选项。

程序在 Windows 上是 `LYNX.exe`，在 macOS 和 Linux 上是 `LYNX`。在 macOS 上完整路径是 `/Applications/LYNX.app/Contents/MacOS/LYNX`。各系统上的选项都相同。

## 把工程渲染为文件

1. 在程序所在的文件夹中打开终端。在 Windows 上，这是安装文件夹，或你解压 zip 的文件夹。
2. 运行 `LYNX.exe --export project.flp out.flac`，换成你自己的工程和输出文件。
3. 等待程序退出。不会打开任何窗口。
4. 阅读写在输出文件旁边的 `out.flac.txt`。对于导入的工程，它会列出哪些内容已转换、哪些是近似还原、哪些被略去，以及找到了哪些插件、缺少哪些插件。

## 打开工程

```
LYNX.exe project.lynx
```

带窗口启动 LYNX SHARE 并打开该文件。它接受 `.lynx`、`.flp`、`.als`、`.flm` 和 `.mid`。相对路径从当前文件夹算起。

```
LYNX.exe --no-plugins project.lynx
```

打开 `.lynx` 工程而不加载其中的第三方插件，与 `文件 › 不加载第三方插件打开...` 相同。每个插件都保留自己的设置。适用于总被某个插件卡住而无法打开的工程。

## 日常使用的选项

| 选项 | 作用 |
|---|---|
| `--export <project> <out>` | 渲染整首歌曲及其尾音，与导出窗口一致。`<out>` 的扩展名决定格式：`.wav`（24 位）、`.flac`、`.ogg` 或 `.mp3`（320 kbps） |
| `--export ... --pattern` | 只渲染当前样式 |
| `--export ... --insert N` | 只渲染混音台插入轨 N 的输出 |
| `--export ... --rate N` | 以另一个采样率渲染，单位为 Hz |
| `--stems <project> <base.wav>` | 为每个带有通道的混音台插入轨写出一个 WAV：`base - 1.wav`、`base - 2.wav`，依此类推 |
| `--export-flp <project> <out.flp>` | 写出 FL Studio 工程。若为 `out.zip`，则写出压缩的循环包 |
| `--export-flp ... --substitute` | 把仅限 LYNX 的插件换成最接近的 FL 插件，而不是把它们渲染为音频 |
| `--export-als <project> <folder>` | 把 Ableton Live 套装写到 `<folder>/<name> Project/<name>.als`，适用于 Live 12 |
| `--export-als ... --live11` | 写出适用于 Live 11 的套装 |
| `--export-bundle <project> <folder>` | 把 `文件 › 导出 › 用于其他应用...` 的包写入该文件夹 |
| `--export-bundle ... --zip` | 压缩该文件夹。它会被 `<folder>.zip` 取代 |
| `--export-bundle ... --no-mixdown` | 包中不含混音文件 |
| `--devices <report.txt>` | 列出这台电脑的音频驱动类型和设备 |
| `--showcase <out.lynx>` | 写出 LYNX SHARE 自己的演示工程 |
| `--lang <id>` | 本次运行使用另一种界面语言，例如 `--lang pt-BR` |
| `--menu-entry on` 或 `关` | 仅限 Linux AppImage：把 LYNX SHARE 添加到应用程序菜单，或将其移除 |

`--export`、`--stems` 和 `--export-flp` 接受 `.lynx`、`.flp` 或 `.als` 作为工程。`--export-als` 还接受 `.flm`。`--export-bundle` 接受 `.lynx`、`.flp` 或 `.als`。

示例：

```
LYNX.exe --export song.lynx song.wav
LYNX.exe --export song.lynx drums.wav --insert 1
LYNX.exe --stems song.lynx song.wav
LYNX.exe --export-flp song.lynx song.flp
LYNX.exe --export-als song.lynx out-folder --live11
LYNX.exe --export-bundle song.lynx out-folder --zip
LYNX.exe --devices devices.txt
LYNX --lang de
```

## 用于测试的选项

这些选项用来检查 LYNX SHARE 本身。写错误报告时很有用。

| 选项 | 作用 |
|---|---|
| `--selftest <report.txt> [demo.lynx]` | 在没有音频设备的情况下运行自检。通过时报告以 `result=OK` 结尾 |
| `--snapshot <prefix> [project]` | 把界面及其窗口绘制为以该前缀命名的 PNG 文件。本 Wiki 里的图片就是这样生成的 |
| `--render <project> <out.wav> <seconds>` | 把当前样式的指定秒数渲染为 24 位 WAV |
| `--plugintest <plugin> <report.txt>` | 加载并测试一个 VST3 或 CLAP 插件 |
| `--sandboxtest <plugin> <report.txt>` | 在独立进程中测试一个插件，包括卡死的插件 |
| `--scantest <folder> <report.txt>` | 在独立进程中扫描文件夹里的插件 |
| `--fldb <report.txt> [project.flp]` | 报告在此处找到的 FL Studio 安装、它的插件数据库，以及工程的插件匹配情况 |
| `--devicetest <type> <device> <report.txt>` | 打开音频驱动，输出一秒静音并报告结果 |
| `--uiperf <project> <report.txt>` | 为混音台、播放列表和通道机架的绘制计时 |

示例：

```
LYNX.exe --selftest report.txt
LYNX.exe --snapshot shots/s project.lynx
LYNX --devicetest PipeWire "System default" out.txt
```

## 须知

- 结果写入文件，而不是输出到终端。`--export` 写入 `<out>.txt`，`--export-flp` 写入 `<out.flp>.txt`，`--export-als` 在该文件夹中写入 `export-als.txt`，`--export-bundle` 在该文件夹中写入 `export-bundle.txt`，`--stems` 写入 `<base.wav>.txt`。
- 程序会读取标准输入。在 shell 循环中，请给每次调用加上 `< /dev/null`。
- `--snapshot` 默认以 1280x720 绘制，除非环境变量 `LYNX_SNAPSHOT_SIZE` 另有设定，例如 `1920x1080`。
- `--lang` 接受 `选项 › 常规设置...` 中所列语言的 id，例如 `en`、`pt-BR`、`de`、`es`、`fr` 或 `ja`。未知的 id 会被忽略。
- `--export` 使用与导出窗口相同的渲染路径，但不包含窗口里的其他选项。需要抖动、位深或标准化时，请使用导出窗口。
- 源代码里还有更多测试选项。它们是内部使用的，可能在不同版本间变化。

## 相关

- [导出](/wiki/exporting/)
- [文件格式](/wiki/file-formats/)
- [从源码构建](/wiki/build-from-source/)
- [故障排除](/wiki/troubleshooting/)
