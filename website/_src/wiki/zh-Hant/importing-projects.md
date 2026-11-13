---
source: 358890815a
---
`檔案 › 開啟...`（`Ctrl+O`）可以開啟 LYNX SHARE 自己的 `.lynx` 專案，也能開啟 `.flp`、`.als`、`.flm` 和 `.mid` 檔案。來自其他程式的專案屬於匯入：它會成為一個新的、尚未儲存的 LYNX SHARE 專案，原始檔案不會被寫入。

## 開啟來自其他程式的專案

1. 選擇 `檔案 › 開啟...`，選取 `.flp`、`.als`、`.flm` 或 `.mid` 檔案。
2. 等待 LYNX SHARE 尋找專案的外掛和取樣。
3. 如果有東西找不到，會開啟 `缺失的檔案` 視窗。請指向你的取樣所在位置，或直接關閉它。
4. 標題列現在會顯示專案的來源，例如「(imported from FL Studio)」。
5. 選擇 `檔案 › 儲存`。LYNX SHARE 會詢問要把 `.lynx` 檔案寫到哪裡。

## 會轉換哪些內容

| 格式 | 已轉換 | 未轉換 |
|---|---|---|
| `.flp`（FL Studio） | 通道、取樣、樣式、滑音和 porta 音符、所有編排、標記、拍號變化、自動化、控制器連結、含路由的混音器、側鏈、軌道 EQ 和原生效果器 | FLEX 包是加密的，不會被開啟。FLEX 通道會得到依預設集名稱選出的合成器音色 |
| `.als`（Live 11 和 12） | MIDI、音訊、群組和回傳軌道，速度、拍號、定位點、循環，音量、聲像、靜音、獨奏和傳送及其自動化，Session 片段和場景，變速標記，機架和鼓機架，以及 Live 自己的許多裝置（EQ Eight、Compressor、Glue Compressor、Reverb、Delay、Auto Filter、Saturator、Utility、Simpler、Drift 等），會變成 LYNX SHARE 自己的 | Live 中沒有 LYNX SHARE 對應項目的裝置（Hybrid Reverb、Grain Delay、Amp、Roar、FM Operator 音色等）會被捨棄並列出。MIDI Effect Rack 不會轉換 |
| `.flm`（FL Studio Mobile） | 速度、拍號、每個通道一個混音插入軌、每條 lane 一條播放清單軌道、樣式、帶偏移和音高的音訊片段、鼓組、DW Sampler 取樣和 `.dwp` 程式、Slicer、MiniSynth、Transistor Bass、3x Osc、GMSynth 和 SuperSaw、效果器、自動化和和弦組 | 其他 FLM 產生器會用內建合成器播放 |
| `.mid` | 音符、拍號和每一次速度變化 | MIDI 檔案不含聲音：通道會得到 LYNX SHARE 合成器或 MIDI Out 通道 |

`.flp` 中的第三方 VST2、VST3 和 CLAP 外掛，以及 `.als` 中的 VST2 和 VST3 外掛，只要你的電腦裡裝有同一個外掛，就會連同儲存的狀態一起載入。

## FL Studio 自己的外掛

FL Studio 的產生器和效果器不會被載入。LYNX SHARE 讀取它們儲存的設定，並用自己重新製作的引擎來播放。每個引擎都有一個品質等級，從 `Exact`（在所有測試過的狀態下與原版位元完全一致）到 `Placeholder`（設定會保留，但外掛靜音或讓音訊直通）。各等級的說明見[從 FL Studio 轉換](/wiki/coming-from-fl-studio/)。

開啟 `.flp` 不需要安裝 FL Studio。有些引擎需要的波形、取樣或脈衝響應，會在執行時從你自己的 FL Studio 安裝中讀取。

## 缺失的取樣

![「缺失的檔案」視窗的 Samples 分頁：找到的檔案為綠色，缺失的為琥珀色，並有 Search folder、Locate 和 Skip](/assets/img/wiki/missing-files.webp)

`取樣` 分頁會列出每個找不到的取樣，以及它儲存的路徑和使用它的對象。

| 指令 | 作用 |
|---|---|
| `搜尋資料夾...` | 在一個資料夾及其子資料夾中搜尋所有缺失的取樣 |
| `定位...` | 詢問某一個取樣在哪裡。在同一個資料夾中找到的其他取樣也會一併套用 |
| `跳過` | 略過該取樣。它的通道保持靜音 |

`套用` 會使用已找到的檔案。

`自動找到` 分頁列出 LYNX SHARE 自己在其他資料夾中找到的檔案。儲存專案即可保留新的位置。

## 缺失的外掛

LYNX SHARE 會先依識別碼、再依檔案、最後依名稱來尋找每個第三方外掛。以 VST2 儲存的專案，可以用同一產品的 VST3 開啟，反之亦然，但這時會使用預設設定。

當某個效果器未安裝時，會由角色相同的 LYNX SHARE 效果器接手插槽：殘響換殘響，壓縮器換壓縮器。

- 插槽會顯示原外掛的名稱，並附上 LYNX SHARE 標記。
- 能讀取已儲存狀態時，會轉換設定。否則替代品從預設的預設集開始。產品、其替代品以及各自轉換內容的對照表，在 [PLUGIN_FALLBACK.md](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/PLUGIN_FALLBACK.md)。
- 原外掛參數的自動化會保留，但不會驅動替代品。
- 原外掛的參照和狀態仍保留在專案中。安裝該外掛後，專案會再次以它開啟。
- 在替代品插槽的右鍵選單中，`旁通此專案的所有LYNX SHARE替代` 可一次關閉全部替代品。

這個開關是 `選項 › 一般設定...` 中的 `用LYNX SHARE對應外掛替代缺失的外掛`，預設為開啟。關閉後，缺失效果器的插槽會從混音器中略去。缺失的樂器不會被替代，它會變成內建合成器，預設集依通道名稱選出。

若要找回真正的外掛，請安裝它，選擇 `選項 › 掃描外掛`，然後重新開啟專案。

## MIDI 檔案

![「匯入 MIDI 資料」視窗：軌道、通道、通道類型和選項](/assets/img/wiki/midi-import-dialog.webp)

`檔案 › 開啟...` 會從 MIDI 檔案建立一個新專案。`檔案 › 匯入 › MIDI檔案...` 會先開啟 `匯入MIDI資料` 視窗，讓你選擇要匯入的內容。

| 設定 | 作用 |
|---|---|
| `要匯入哪些軌道？` | 所有軌道，或單一軌道 |
| `要匯入哪些通道？` | 點擊某個 MIDI 通道可將其排除。按右鍵則只保留那一個 |
| `通道類型` | `LYNX SHARE合成器` 或 `MIDI Out` |
| `開始新專案` | 開始一個空白專案。關閉時：加入到已開啟的專案 |
| `每個軌道建立一個通道` | 關閉時：每個 MIDI 通道建立一個通道 |
| `為新通道設定混音軌道` | 把新通道路由到連續的混音插入軌 |
| `匯入拍號` | 檔案的拍號會成為專案的拍號 |

在[鋼琴捲軸](/wiki/piano-roll/)中按 `Ctrl+M`，可把 MIDI 檔案匯入目前的通道。

## 須知

- `說明 › 缺失的檔案...` 可在之後再次開啟該視窗。`說明 › Last FL Studio import report` 會列出被近似處理、被省略或未安裝的內容。
- Live 套裝的 Session 片段會放進片段啟動版面的片段格線。`選項 › 一般設定...` 中的 `Live 專案的工作階段片段` 則會改為把它們放在播放清單上。

## 相關頁面

- [檔案格式](/wiki/file-formats/)
- [從 FL Studio 轉換](/wiki/coming-from-fl-studio/)
- [從 Ableton Live 轉換](/wiki/coming-from-ableton/)
- [外掛](/wiki/plugins/)
- [匯出](/wiki/exporting/)
