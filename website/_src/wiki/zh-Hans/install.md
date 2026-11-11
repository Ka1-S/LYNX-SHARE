---
source: 8a120ab7de
---
每个版本都在[发布页面](https://github.com/Ka1-S/LYNX-SHARE/releases)提供 Windows、macOS 和 Linux 的构建。这些文件由 GitHub Actions 根据带标签的源代码构建，并连同它们的 SHA-256 一起列在 `SHA256SUMS.txt` 中。[下载](/download/)页面链接到当前版本。

## 在 Windows 上安装

Windows 10 或 11，64 位。

1. 下载 `LYNX-<version>-setup-x64.exe`。
2. 运行它。它只为你的用户安装，不需要管理员权限。
3. SmartScreen 可能会发出警告，因为这个可执行文件尚未进行代码签名。选择 `More info`，然后选择 `Run anyway`。
4. 从开始菜单或桌面快捷方式启动 LYNX SHARE。

安装程序会让 LYNX SHARE 成为打开 `.lynx` 工程的程序。它还会在 `.flp` 文件的 *打开方式* 中提供 LYNX SHARE，同时不更改 FL Studio 的默认设置。

还有另外两种方式：

| 方式 | 文件或位置 | 作用 |
|---|---|---|
| 便携 zip | `LYNX-<version>-windows-x64.zip` | 解压到任意位置并运行 `LYNX.exe`。不会安装任何东西。 |
| Microsoft Store | [Store 上的 LYNX SHARE](https://apps.microsoft.com/detail/9NQRFBFDCX6N) | 由 Store 安装并保持更新。 |

## 在 macOS 上安装

macOS 11 Big Sur 或更新版本。一个通用的 `LYNX-<version>-macos-universal.dmg` 同时适用于 Intel 和 Apple 芯片。

1. 打开 `.dmg`，把 LYNX SHARE 拖到 *应用程序* 中。
2. 这个应用没有经过 Apple 公证，也没有 Developer ID 签名，所以首次启动需要多一步操作。在 *应用程序* 中右键单击（或按住 Control 单击）LYNX SHARE，选择 *打开*，然后再次选择 *打开*。
3. 在 macOS 15 Sequoia 或更新版本上，先尝试打开一次，然后进入 *系统设置 › 隐私与安全性*，单击 *仍要打开*。

用终端的替代方法：

```
xattr -dr com.apple.quarantine /Applications/LYNX.app
```

你第一次录音时，macOS 会请求麦克风权限。

## 在 Linux 上安装

x86_64，glibc 2.35 或更新（Ubuntu 22.04+、Debian 12+、Fedora 36+、当前的 Arch 或 Manjaro）。LYNX SHARE 需要 X11 或 XWayland。音频通过 PipeWire、JACK 或 ALSA 输出，为此不需要额外安装任何东西。

AppImage（`LYNX-<version>-x86_64.AppImage`）：

```
chmod +x LYNX-*.AppImage
./LYNX-*.AppImage
```

有些发行版需要 libfuse2 才能运行 AppImage（Ubuntu 22.04 上是 `libfuse2`，24.04 上是 `libfuse2t64`，Arch 上是 `fuse2`，Fedora 上是 `fuse-libs`）。首次启动时，设置向导会提议把 LYNX SHARE 添加到应用程序菜单，并用它打开 `.lynx` 文件。之后你可以在 `选项 › 常规设置...` 中切换，或者在终端中用 `./LYNX-*.AppImage --menu-entry on`（或 `关`）切换。

如果你不想安装 libfuse2，请使用 `LYNX-<version>-linux-x86_64.tar.gz`：解压后运行 `./install.sh`。这会为你的用户把 LYNX SHARE 安装到 `~/.local` 下，并带有菜单项、图标和 `.lynx` 文件类型。

## 设置保存在哪里

| 系统 | 设置文件夹 |
|---|---|
| Windows | `%APPDATA%\LYNX` |
| macOS | `~/Library/Application Support/LYNX` |
| Linux | `~/.config/LYNX`（或在 `$XDG_CONFIG_HOME` 之下） |

该文件夹保存你的选项、所选语言（`language.txt`）、你的快捷键（`keys.json`）、你的插件文件夹（`plugin-folders.txt`）以及已扫描插件的列表（`plugins.xml`）。

## 更新

启动时，LYNX SHARE 会向 GitHub 询问最新的版本号。不会发送任何关于你的信息。有新版本发布时，会出现一个提示栏，显示 `LYNX SHARE <version> is available`，带有 `新功能` 按钮和一个更新按钮。`帮助 › 检查更新...` 可随时查询。

| 构建 | 如何更新 |
|---|---|
| Windows 安装程序 | `更新` 下载新的安装程序，询问是否保存未保存的更改，然后安装并重启 |
| Windows 便携 zip、macOS | `更新` 下载新文件并在其所在文件夹中显示 |
| Linux AppImage | `更新` 就地更新 AppImage 文件。Gear Lever 和 AppImageUpdate 也能从外部做到同样的事 |
| Linux `.tar.gz` | `下载...` 打开下载页面 |
| Microsoft Store | 由 Store 更新。这个构建自己没有更新检查 |

要关闭检查，请打开 `选项 › 常规设置...`，取消勾选 `自动检查更新`。

## 卸载

- Windows 安装程序：在 Windows 的 *应用和功能* 列表中卸载 LYNX SHARE。
- 便携 zip：删除你解压出来的文件夹。
- Linux `.tar.gz`：运行 `./install.sh --uninstall`。

## 须知

- 在项目的代码签名申请获批之前，Windows 构建都是未签名的。macOS 应用只有临时签名（ad-hoc）。Linux 软件包没有签名。
- 要校验下载的文件，请把它的 SHA-256 与发布页面上 `SHA256SUMS.txt` 中对应的那一行比较。
- 安装程序、zip、`.dmg` 和 `.tar.gz` 都包含演示工程 `Demo.lynx`。
- 用关闭按钮关闭更新提示栏，会跳过该版本。

## 相关

- [从这里开始](/wiki/getting-started/)
- [音频设置](/wiki/audio-setup/)
- [故障排除](/wiki/troubleshooting/)
- [下载](/download/)
- [更新](/updates/)
