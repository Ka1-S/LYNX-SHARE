---
source: 6d1d7a664e
---
## 驅動程式和輸出

![設定的 Audio 頁面](/assets/img/learn/audio-setup-1.webp)

1. 開啟 `選項 › 音訊設定...`。
2. 選擇驅動程式：Windows 上用 ASIO（你的音訊介面自帶的，或 FL Studio ASIO），Mac 上用 Core Audio，Linux 上用 PipeWire 或 ALSA。
3. `輸出裝置` 是你聽到 LYNX 聲音的地方。`System default` 會跟隨電腦目前使用的輸出。

## 緩衝區大小

![各種緩衝區大小及其延遲](/assets/img/learn/audio-setup-2.webp)

1. `緩衝區大小`：緩衝區越小，彈奏鍵盤時反應越快；越大，CPU 越從容。
2. 有爆音或掉音？把它調大。彈奏時聲音太晚出現？把它調小。

## 測試聲音

![Help，Test audio output](/assets/img/learn/audio-setup-3.webp)

1. `播放測試音` 會透過所選的輸出播放一段短聲音。
2. 在任何地方，`說明 › 測試音訊輸出...` 都能做同樣的事。

另請參閱[音訊設定](/wiki/audio-setup/)和[疑難排解](/wiki/troubleshooting/)。
