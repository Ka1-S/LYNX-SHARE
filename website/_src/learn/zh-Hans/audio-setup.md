---
source: 297095d385
---
## 驱动和输出

![设置的 Audio 页面](/assets/img/learn/audio-setup-1.webp)

1. 打开 `选项 › 音频设置...`。
2. 选择驱动。在 Windows 上，`LYNX ASIO（内置）` 适用于任何设备，不需要专门的驱动；你的音频接口自带的 ASIO 驱动、FL Studio ASIO 以及 `Windows音频（共享）` 或 `Windows音频（独占，最低延迟）` 也都可以选。Mac 上用 Core Audio，Linux 上用 PipeWire 或 ALSA。
3. `输出设备` 是你听到 LYNX 声音的地方。`System default` 会跟随电脑当前使用的输出。
4. `输入设备` 是你录音的来源（麦克风、吉他输入）。`采样率` 通常是 44100 或 48000 Hz。

## 缓冲区大小

![各种缓冲区大小及其延迟](/assets/img/learn/audio-setup-2.webp)

1. `缓冲区大小`：缓冲区越小，弹奏键盘时响应越快；越大，CPU 越从容。
2. 有爆音或掉音？调大它。弹奏时声音来得晚？调小它。

## 测试声音

![Help，Test audio output](/assets/img/learn/audio-setup-3.webp)

1. `播放测试音` 会通过所选输出播放一段短声音。
2. 在任何地方，`帮助 › 测试音频输出...` 都能做同样的事。

## 录好的片段偏晚？

1. 音频接口需要一点时间把声音送进来再送出去，所以录好的片段可能会稍微偏晚。
2. 在同一个音频页面的 `录音延迟补偿` 下，用一根线把一个输出连到一个输入（或使用音频接口的回环功能），然后点击 `测量往返延迟`。
3. LYNX 会播放几下咔嗒声，测量它们回来时晚了多少，此后会把录好的片段对准时间。

另见[音频设置](/wiki/audio-setup/)和[故障排除](/wiki/troubleshooting/)。
