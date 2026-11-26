---
source: 297095d385
---
## 驅動程式和輸出

![設定的 Audio 頁面](/assets/img/learn/audio-setup-1.webp)

1. 開啟 `選項 › 音訊設定...`。
2. 選擇驅動程式。在 Windows 上，`LYNX ASIO（內建）` 適用於任何裝置，不需要專屬的驅動程式；你的音訊介面自帶的 ASIO 驅動程式、FL Studio ASIO 以及 `Windows音訊（共享）` 或 `Windows音訊（獨佔，最低延遲）` 也都可以選。Mac 上用 Core Audio，Linux 上用 PipeWire 或 ALSA。
3. `輸出裝置` 是你聽到 LYNX 聲音的地方。`System default` 會跟隨電腦目前使用的輸出。
4. `輸入裝置` 是你錄音的來源（麥克風、吉他輸入）。`取樣率` 通常是 44100 或 48000 Hz。

## 緩衝區大小

![各種緩衝區大小及其延遲](/assets/img/learn/audio-setup-2.webp)

1. `緩衝區大小`：緩衝區越小，彈奏鍵盤時反應越快；越大，CPU 越從容。
2. 有爆音或掉音？把它調大。彈奏時聲音太晚出現？把它調小。

## 測試聲音

![Help，Test audio output](/assets/img/learn/audio-setup-3.webp)

1. `播放測試音` 會透過所選的輸出播放一段短聲音。
2. 在任何地方，`說明 › 測試音訊輸出...` 都能做同樣的事。

## 錄好的片段偏晚？

1. 音訊介面需要一點時間把聲音送進來再送出去，所以錄好的片段可能會稍微偏晚。
2. 在同一個音訊頁面的 `錄音延遲補償` 下，用一條線把一個輸出接到一個輸入（或使用音訊介面的迴路功能），然後點擊 `測量往返延遲`。
3. LYNX 會播放幾下喀嗒聲，測量它們回來時晚了多少，之後會把錄好的片段對準時間。

另請參閱[音訊設定](/wiki/audio-setup/)和[疑難排解](/wiki/troubleshooting/)。
