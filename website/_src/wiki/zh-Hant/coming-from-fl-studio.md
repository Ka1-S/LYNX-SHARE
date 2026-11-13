---
source: 3bf7e0c0ab
---
LYNX SHARE 保留了你熟悉的工作流程：通道機架、樣式、播放清單和混音器，按鍵也一樣。它可以直接開啟 `.flp` 專案，也能把專案寫回去。

LYNX SHARE 是獨立的程式，與 Image-Line 沒有任何隸屬、認可或贊助關係，也不含任何 Image-Line 的程式碼。FL Studio 是 Image-Line Software 的註冊商標。

## 開啟 .flp 專案

1. 選擇 `檔案 › 開啟...`（`Ctrl+O`），選取 `.flp`。
2. LYNX SHARE 會解析專案的外掛和取樣，然後開啟它。標題會顯示「(imported from FL Studio)」。
3. 選擇 `說明 › Last FL Studio import report`，查看哪些內容被近似處理、略過或找不到。
4. 選擇 `說明 › 缺失的檔案...`，查看缺少的取樣和外掛。
5. 選擇 `檔案 › 儲存`。它會詢問要把 `.lynx` 檔案寫到哪裡。`.flp` 本身絕不會被改寫。

## 對應關係

| 在 FL Studio 中 | 在 LYNX SHARE 中 |
|---|---|
| 播放清單、通道機架、鋼琴捲軸、混音器 | 名稱相同，按鍵為 `F5`、`F6`、`F7` 和 `F9` |
| 外掛選擇器 | `F8` |
| 瀏覽器 | `Alt+F8` |
| 樣式 / 歌曲模式 | `L` |
| 復原 | `Ctrl+Z` 復原，復原之後再按則是重做。`Ctrl+Alt+Z` 向後復原 |
| 儲存新版本 | `Ctrl+Alt+S` |
| 匯出 | `Ctrl+R`，MP3 是 `Ctrl+Shift+R`，MIDI 是 `Ctrl+Shift+M` |
| 混音器插入軌、傳送和側鏈 | 相同，每個插入軌有 10 個效果器插槽 |
| Fruity Wrapper | 第三方外掛直接被代管，見[第三方外掛](/wiki/plugins/) |
| 原生產生器和效果器 | 在 LYNX SHARE 重新製作的引擎上播放，見下方 |
| `.flp` 專案檔案 | `.lynx` 專案檔案 |

`說明 › 快速鍵` 列出所有按鍵。

## 會匯入什麼

- 通道、取樣、樣式、滑音和連音音符、所有編排、標記、拍號變化、自動化和控制器關聯。
- 混音器：路由、電平、側鏈、軌道 EQ，以及帶有已儲存設定的原生效果器。
- Fruity Wrapper 內的第三方 VST3、VST2 和 CLAP 外掛：會換成你電腦上安裝的同一個外掛，並帶上它儲存的狀態。

## FL Studio 的原生外掛

FL Studio 自己的產生器和效果器不會被載入。LYNX SHARE 用重新製作的引擎來播放它們，每一個都有品質等級：

| 等級 | 意義 |
|---|---|
| Exact | 在所有測試過的狀態下，都與原版逐位元相同 |
| Near-exact | 大多數狀態逐位元相同，其餘的差異低於約 -100 dB |
| Close | 演算法相同，在幾乎所有測試過的狀態下與原版的差距在 -60 dB 以內 |
| Approximate | 儲存的設定被對應到 LYNX SHARE 內建的合成器或效果器 |
| Placeholder | 狀態會保留，外掛則靜音或讓音訊直通 |

每個引擎的等級，以及各自還缺少什麼，都在 [PLUGINS.md](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/PLUGINS.md) 中。另見[內建樂器與效果器](/wiki/built-in/)。

## 哪些功能需要你自己的 FL Studio 安裝

開啟 `.flp` 不需要安裝 FL Studio。如果同一台電腦上裝有它，會多出兩樣東西：

- FL Studio 的原廠取樣及其外掛資料庫，這樣不必掃描就能找到取樣和外掛。
- 部分引擎需要的內建波形、取樣、脈衝響應和表格（FL Keys、Soundgoodizer、Maximus，以及 PLUGINS.md 中標有劍號的其他引擎）。它們在執行時從你的安裝中讀取，並不隨 LYNX SHARE 發行。

在 Linux 上，LYNX SHARE 會在 Wine 下尋找 Windows 版的 FL Studio。在 macOS 上會尋找應用程式套件，但這一點尚未測試。

## 匯出回 .flp

1. 選擇 `檔案 › 匯出 › FL Studio 專案...`。
2. 選擇 `FL Studio 專案 (.flp)` 或 `壓縮循環包 (.zip，含全部取樣)`。
3. 選擇 FL Studio 沒有的東西要如何處理：算繪為音訊（聽起來相同），或使用最接近的 FL 外掛（仍可編輯，聽起來可能不同，且僅限 LYNX 的樂器會被略過）。

這個檔案是 FL Studio 2026 專案。取樣器、音訊片段、FL 自己的產生器和效果器、第三方外掛（在 Fruity Wrapper 內）、樣式、播放清單、混音器和自動化，都會連同設定一起轉過去。報告會列出哪些完全照原樣轉出，哪些被算繪、取代或略過。

## FL Studio Mobile (.flm)

`檔案 › 開啟...` 也能開啟 `.flm` 專案。匯入是部分的。

- 會匯入：速度、拍號、每個 FLM 通道一條混音器插入軌、每條車道一條播放清單軌道、樣式（每次循環作為一個片段）、帶有位移和音高的音訊片段、鼓組、DW Sampler 取樣和 `.dwp` 程式與 Slicer（用 FLM 自己的引擎）、MiniSynth、Transistor Bass、3x Osc、GMSynth 和 SuperSaw 樂器、FLM 的效果器（作為 Pocket 效果器），以及音量、聲像、靜音、獨奏、彎音、傳送和樂器參數的自動化，還有和弦組和調性鎖定。
- 不會匯入：FLM 的其他樂器，它們會改用內建合成器播放。
- 取樣會在 `.flm` 旁邊以及 FL Studio 的 FLM 資源副本中尋找。找不到的 `.dwp` 會用替身播放。

## 須知

- FLEX 套件是加密的，絕不會被開啟。FLEX 通道會得到依預設集名稱挑選的合成器音色。
- 調變器不會出現在 `.flp` 匯出中，匯出報告會說明這一點。
- 匯入報告會列出哪些由內建合成器近似處理、哪些沒有對應物、哪些外掛找到了或沒有安裝、哪些效果器被略過或只重現了一部分，以及缺少多少取樣。
- 沒有安裝的第三方效果器，會由角色相同的 LYNX SHARE 效果器取代。原外掛仍保留在專案中。

## 相關頁面

- [內建樂器與效果器](/wiki/built-in/)
- [第三方外掛](/wiki/plugins/)
- [檔案格式](/wiki/file-formats/)
- [快速鍵](/wiki/shortcuts/)
- [只有 LYNX SHARE 能用 FL 設定做到的事](/why/#fl-settings)
