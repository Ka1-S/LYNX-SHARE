---
source: c82c79e958
---
LYNX SHARE 有三種版面。如果你從 Logic、Cubase、Reaper、Pro Tools 或 Studio One 轉過來，請從 **搭配混音器的簡易版面** 開始：時間軸上每個樂器或聲音一條音軌，和你熟悉的一樣。想用樣式時，還有模式版面，也就是 FL Studio 的工作方式。

## 你的第一步

1. **選擇版面。** 第一次啟動 LYNX SHARE 時，歡迎卡片會問你從哪裡轉過來。點擊 `Logic、Cubase、Reaper、Pro Tools 或 Studio One`：簡易版面會開啟它的新音軌卡片，混音器會帶著你的第一條音軌出現。之後，`檢視 › 版面樣式 › 簡易工作流程` 可隨時切換到它，`F9` 可開啟混音器。
2. **新增音軌。** `+ 新增音軌` 提供 `人聲 / 音訊`、`樂器`、`鼓`、`取樣器`、`吉他 / 貝斯`、`匯入` 和 `循環素材`。用電腦按鍵、螢幕鍵盤或 MIDI 鍵盤來演奏樂器音軌。
3. **錄音。** 在 `選項 › 音訊設定...` 中選擇你的音訊裝置，然後見[錄下你的聲音](/learn/record-voice/)。

## 哪些相同

- 時間軸上的音軌，附有音訊、MIDI 和自動化片段。
- 音訊和 MIDI 錄音，支援循環錄音和合成錄音。
- 附有插入效果、傳送、側鏈、群組和延遲補償的混音器。
- 第三方外掛：所有系統上的 VST3 和 CLAP，macOS 上的 Audio Units，Linux 上的 LV2，Windows 和 Linux 上的 VST2。
- 匯出歌曲、依音軌匯出分軌，以及匯出 MIDI。

## 哪些不同

- **音軌和混音器插入軌是分開的。** 在簡易版面中，每條新音軌都有自己的通道和混音器軌道。在模式版面中，你要像在 FL Studio 裡一樣自己把通道路由到插入軌。
- **樣式。** 在模式版面中，音符放在樣式裡，你把樣式擺到播放清單上，一個樣式可以包含多個樂器。
- **按鍵沿用 FL Studio。** `Ctrl+Z` 復原一步，再按一次則是重做。`Ctrl+Alt+Z` 可以復原得更遠。
- **不能匯入 Logic、Cubase、Reaper 或 Pro Tools 的專案。** 請用 DAWproject 檔案、分軌或 MIDI 來搬移專案（見下方）。

## 按鍵

| 要做什麼 | 按鍵 |
|---|---|
| 播放，或停止並返回 | `Space` |
| 開啟或關閉錄音 | `R` |
| 混音器 | `F9` |
| 鋼琴捲軸 | `F7` |
| 播放清單 | `F5` |
| 瀏覽器 | `Alt+F8` |
| 開啟、儲存 | `Ctrl+O`、`Ctrl+S` |
| 復原，以及復原得更遠 | `Ctrl+Z`、`Ctrl+Alt+Z` |
| 匯出音訊、MP3、MIDI | `Ctrl+R`、`Ctrl+Shift+R`、`Ctrl+Shift+M` |

`說明 › 快速鍵` 列出了所有按鍵。見[快速鍵](/wiki/shortcuts/)。

## 搬移專案

- **Cubase、Studio One 和 Bitwig**：在那邊儲存一個 DAWproject 檔案（`.dawproject`），然後用 `檔案 › 開啟...` 開啟。音軌、資料夾、片段、音符、附扭曲標記的音訊、自動化、速度、標記、附傳送的混音器，以及附狀態的 VST3、VST2 和 CLAP 外掛都會帶過來。`檔案 › 匯出 › DAWproject (.dawproject)...` 則走相反的方向。
- **Logic、Reaper 和 Pro Tools**：把每條音軌從歌曲開頭匯出為音訊或 MIDI 檔案，放進同一個資料夾。在 LYNX SHARE 中點擊 `+ 新增音軌`，再點擊 `匯入` 旁邊的 `資料夾...`：每個檔案會成為一條從開頭開始的音軌。
- **匯出回去**：匯出視窗會為每個混音器插入軌或每個通道寫出一個檔案，`檔案 › 匯出 › MIDI檔案...` 則寫出音符。

見[檔案格式](/wiki/file-formats/)和[匯出](/wiki/exporting/)。

## 須知

- Logic 是 Apple Inc. 的商標，Cubase 是 Steinberg Media Technologies 的商標，Reaper 是 Cockos Incorporated 的商標，Pro Tools 是 Avid Technology 的商標，Studio One 是 PreSonus Audio Electronics 的商標，Bitwig 是 Bitwig GmbH 的商標。LYNX SHARE 是獨立的程式，與它們沒有隸屬關係，也未獲其認可。
- 片段啟動版面會在時間軸旁邊加上一個附場景的片段網格。見[版面](/wiki/layouts/)。

## 相關頁面

- [版面](/wiki/layouts/)
- [混音器](/wiki/mixer/)
- [錄音](/wiki/recording/)
- [從 FL Studio 轉過來](/wiki/coming-from-fl-studio/)
- [學習：教學](/learn/)
