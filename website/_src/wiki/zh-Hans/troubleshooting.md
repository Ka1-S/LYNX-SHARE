---
source: a3748d4a2a
---
下面每个标题是一个问题。如果没有你的问题，请按末尾所述的方式报告。

## 启动程序

### Windows：首次启动时 SmartScreen 发出警告

这个可执行文件尚未进行代码签名，所以你第一次运行时 SmartScreen 可能会发出警告。

1. 单击 `More info`。
2. 单击 `Run anyway`。

安装程序不需要管理员权限。

### macOS：“无法打开”

这个应用没有经过 Apple 公证，也没有 Developer ID 签名，所以首次启动需要多一步操作。

1. 把 LYNX SHARE 拖到 `Applications`。
2. 右键单击（按住 Control 单击）它，选择 `打开`。
3. 再次单击 `打开`。

在 macOS 15 Sequoia 或更新版本上，先尝试打开一次，然后进入 `System Settings › Privacy & Security`，单击 `Open Anyway`。或者在终端中运行：

```
xattr -dr com.apple.quarantine /Applications/LYNX.app
```

你第一次录音时，macOS 会请求麦克风权限。

### Linux：AppImage 无法启动

LYNX SHARE 需要 x86_64 系统，glibc 2.35 或更新（Ubuntu 22.04、Debian 12、Fedora 36、当前的 Arch 或 Manjaro），以及 X11 或 XWayland。

1. 用下面的两条命令，让文件可执行并运行它。
2. 如果仍然无法启动，请安装 libfuse2。该软件包在 Ubuntu 22.04 上是 `libfuse2`，在 Ubuntu 24.04 上是 `libfuse2t64`，在 Arch 上是 `fuse2`，在 Fedora 上是 `fuse-libs`。
3. 如果你不想安装它，可以改为下载 `.tar.gz`，解压后运行 `./install.sh`。

```
chmod +x LYNX-*.AppImage
./LYNX-*.AppImage
```

`./install.sh` 会为你的用户把 LYNX SHARE 安装到 `~/.local` 下，并带有菜单项。`./install.sh --uninstall` 则将其移除。

## 声音

### 没有声音，或音频设备不对

1. 打开 `选项 › 音频设置...`。
2. 选择一个驱动。
3. 选择一个输出设备。

| 系统 | 驱动 |
|---|---|
| Windows | Windows Audio (WASAPI)、DirectSound、ASIO 和内置的 LYNX ASIO 驱动 |
| macOS | Core Audio |
| Linux | PipeWire（原生）、JACK 和 ALSA。首次启动时，LYNX SHARE 会打开其中第一个有设备的驱动 |

ASIO 是 Windows 的驱动模型，只存在于 Windows 上。`选项 › 设置向导（语言、音频、插件文件夹）...` 也会测试音频设备。见[音频设置](/wiki/audio-setup/)。

### Linux：声音断断续续

音频线程应以实时优先级运行。

1. 打开 `选项 › 音频设置...`，查看 `状态`。它显示线程是否获得了实时优先级。
2. 如果没有，请安装 rtkit（`sudo apt install rtkit`、`sudo dnf install rtkit` 或 `sudo pacman -S rtkit`），或者像 JACK 的配置那样，把你自己加入带有实时限制的 `音频` 组。
3. 注销后重新登录。

详情见 [LINUX.md](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/LINUX.md)。

## 插件

### 插件崩溃

让它在自己的进程中运行，这样停止的只是这个插件。

1. 打开 `选项 › 常规设置...`。
2. 打开 `在独立进程中运行插件（防止插件崩溃影响LYNX SHARE）`。
3. 重新打开工程。

在自己进程中崩溃或停止响应的插件会被静音。保存工程并重新打开，即可再次加载该插件。见[第三方插件](/wiki/plugins/)。

### 插件不在列表中

1. 选择 `选项 › 扫描插件`。
2. 如果仍然缺失，请打开 `选项 › 插件管理器...`，单击 `插件文件夹...`，添加该插件所在的文件夹。
3. 查看 `失败` 标签页。扫描失败的插件文件会被跳过。在 Windows 上，原因记录在 `%APPDATA%\LYNX\scan-failures.log` 中。
4. 单击 `全部重新扫描...` 可再次尝试失败的文件。

### 工程用到了未安装的插件

效果器会被作用相同的 LYNX SHARE 效果器替代，其设置会保留在工程中。`帮助 › 缺失的文件...` 会列出缺失的内容。见[内置乐器和效果器](/wiki/built-in/)。

### 视频在 Linux 上不工作

视频轨道和 `Export with video` 需要 FFmpeg。LYNX SHARE 会告诉你如何安装它。

## 崩溃报告

![下次启动时显示的崩溃报告询问，带有 Send、Show what will be sent 和 Don't send](/assets/img/wiki/crash-report.webp)

崩溃报告需要你主动选择加入。崩溃或卡死之后，下次启动时会询问是否发送报告。

| 命令 | 作用 |
|---|---|
| `显示将要发送的内容` | 显示确切的文字 |
| `改为在GitHub上报告` | 打开 GitHub 的问题表单，内容已预先填好。文件需要你自己附上 |
| `打开报告所在的文件夹` | 在你的电脑上显示报告文件 |
| `Include file names (projects, samples)` | 关闭时，只发送每个文件的扩展名 |
| `始终发送，不再询问` | 以后发送报告时不再询问 |

`选项 › 常规设置...` 中的 `崩溃报告` 可设为 `Ask`（默认）、`始终发送` 或 `从不发送`。

报告包含 LYNX SHARE 版本、操作系统、CPU、显卡及其驱动、屏幕尺寸、音频驱动和设备、崩溃或卡死文件、本次会话的最后 60 步，以及所加载的第三方插件的名称和版本。

它不包含音频、工程、采样、你的电脑名或你的用户名。服务器不保存 IP 地址。已发送的 id 列在设置文件夹里的 `crash-reports\sent.log` 中。要删除某份报告，请带上它的 id 提交一个 issue。完整文本见[隐私政策](https://github.com/Ka1-S/LYNX-SHARE/blob/main/PRIVACY.md)。

## 报告问题

![报告问题窗口：一个描述框、一个可选的电子邮件地址和 Send 按钮](/assets/img/wiki/report-problem.webp)

1. 选择 `帮助 › 报告问题...`。
2. 描述发生了什么。电子邮件地址是可选的，想收到回复时再填。
3. 如果问题是崩溃，请保持勾选 `附上最新的崩溃/卡死文件和本次会话的步骤`。
4. 单击 `显示将要发送的内容` 检查文字。
5. 单击 `发送`。在此之前不会发送任何内容。

你也可以在 [github.com/Ka1-S/LYNX-SHARE/issues](https://github.com/Ka1-S/LYNX-SHARE/issues) 提交 issue。

## 须知

- 崩溃之后，你的工程备份副本在 LYNX SHARE 的自动保存文件夹中。
- 如果崩溃与某个插件有关，询问中会提议今后让该插件在独立进程中运行。
- 设置文件夹在 Windows 上是 `%APPDATA%\LYNX`，在 macOS 上是 `~/Library/Application Support/LYNX`，在 Linux 上是 `~/.config/LYNX`。

## 相关

- [音频设置](/wiki/audio-setup/)
- [第三方插件](/wiki/plugins/)
- [安装](/wiki/install/)
- [常见问题](/faq/)
- [崩溃保护](/why/#crash)
