---
source: 6d1d7a664e
---
## 驱动和输出

![设置的 Audio 页面](/assets/img/learn/audio-setup-1.webp)

1. 打开 `选项 › 音频设置...`。
2. 选择驱动：Windows 上用 ASIO（你的音频接口自带的，或 FL Studio ASIO），Mac 上用 Core Audio，Linux 上用 PipeWire 或 ALSA。
3. `输出设备` 是你听到 LYNX 声音的地方。`System default` 会跟随电脑当前使用的输出。

## 缓冲区大小

![各种缓冲区大小及其延迟](/assets/img/learn/audio-setup-2.webp)

1. `缓冲区大小`：缓冲区越小，弹奏键盘时响应越快；越大，CPU 越从容。
2. 有爆音或掉音？调大它。弹奏时声音来得晚？调小它。

## 测试声音

![Help，Test audio output](/assets/img/learn/audio-setup-3.webp)

1. `播放测试音` 会通过所选输出播放一段短声音。
2. 在任何地方，`帮助 › 测试音频输出...` 都能做同样的事。

另见[音频设置](/wiki/audio-setup/)和[故障排除](/wiki/troubleshooting/)。
