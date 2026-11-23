---
source: e1ff018baf
---
LYNX SHARE 將專案儲存為 `.lynx` 檔案。它也能開啟其他程式的專案、載入常見的音訊檔案，並匯出音訊、MIDI 和給其他程式使用的專案。下表列出 0.1.31 alpha 的所有格式。

從其他程式的格式開啟的專案屬於匯入。此時 `檔案 › 儲存` 會詢問要把 `.lynx` 檔案寫到哪裡，原始檔案絕不會被更改。

## 開啟其他程式的專案

1. 選擇 `檔案 › 開啟...`（`Ctrl+O`）。
2. 選取 `.flp`、`.als`、`.flm`、`.dawproject`、`.band`、`.blx`、`.mid` 或 `.zip` 檔案。
3. 請選擇 `說明 › 上次專案匯入報告`，查看哪些內容被近似處理或略過。
4. 選擇 `檔案 › 儲存`，並選擇 `.lynx` 檔案要寫到哪裡。

## 格式

| 格式 | 開啟 | 儲存 / 匯出 | 備註 |
|---|---|---|---|
| `.lynx` | `檔案 › 開啟...` | `檔案 › 儲存`，並有自動儲存 | LYNX SHARE 自己的專案格式（JSON）。`檔案 › 為舊版 LYNX 儲存副本...` 會寫出一份 0.1.27 或 0.1.28 能開啟的副本，並列出該版本無法保留的內容 |
| `.zip` | `檔案 › 開啟...` 會把它解壓縮到資料夾，並開啟裡面的專案 | `檔案 › 匯出 › 壓縮循環包...` | 也能開啟 FL Studio 的壓縮循環包 |
| `.lynxpack` | 先顯示摘要再開啟。其中一部分會合併到主持人的專案 | `檔案 › 線上工作階段 › 傳送專案...` | 在[線上協作](/wiki/online-sessions/)中傳送的專案或部分內容。第三方外掛的部分會以音訊算繪加上外掛設定的方式傳送 |
| `.flp` | `檔案 › 開啟...` | `檔案 › 匯出 › FL Studio 專案...`，一個 FL Studio 2026 專案或壓縮循環包 | 原生外掛在重新製作的引擎上播放，精確度各不相同。FLEX 套件絕不會被開啟。匯出時，僅限 LYNX 的樂器和效果器會被算繪為音訊，或換成最接近的 FL 外掛，調變器則會被略過。見[從 FL Studio 轉換](/wiki/coming-from-fl-studio/) |
| `.flm` | `檔案 › 開啟...` | `檔案 › 將副本儲存為 FL Studio Mobile 專案 (.flm)...` | FL Studio Mobile 專案。部分支援：MiniSynth、Transistor Bass、3x Osc、GMSynth、SuperSaw、DW Sampler、Slicer 和 DirectWave 以外的樂器會改用內建合成器播放。匯出是一份副本：保留音符、速度、混音器、FL Studio Mobile 自帶的樂器、DW Sampler、3x Osc、鼓組、效果器，以及混音器音量和聲像、效果器、DW Sampler 和 GMSynth 的自動化，取樣放在旁邊。副本中，其他樂器的音符保留在 MiniSynth 上，沒有 FLM 模組的效果器和其他目標（合成器自身的旋鈕、第三方外掛）的自動化會被省略；儲存前會有一個視窗列出這些內容。 |
| `.als` | `檔案 › 開啟...`，Live 11 和 12 的套裝 | `檔案 › 匯出 › Ableton Live 套裝 (.als)...`，適用於 Live 12 或 Live 11 | 匯入時，Live 中沒有 LYNX SHARE 對應項目的裝置會被捨棄。匯出時，LYNX SHARE 自己的樂器和效果器不會被轉換（Helix 會變成 Wavetable），效果器參數的自動化和插入軌之間的路由會被捨棄。見[從 Ableton Live 轉換](/wiki/coming-from-ableton/) |
| `.dawproject` | `檔案 › 開啟...` | `檔案 › 匯出 › DAWproject (.dawproject)...` | Bitwig、Studio One、Cubase 等程式使用的開放交換格式：軌道、資料夾、片段、音符、帶 warp 標記的音訊、自動化、速度、標記、帶傳送的混音器，以及帶狀態的 VST3、VST2 和 CLAP 外掛。匯入時，音符效果器和 MIDI 表情會被略過；匯出時，片段網格、其他編排和部分逐音符的細節會被略過。兩個方向都會列出被略過的內容 |
| `.band` | `檔案 › 開啟...`（專案套件、其中的 `projectData` 檔案或它的 zip） | 否 | GarageBand 10 或 GarageBand for iOS 專案，作為簡易工作流程的音軌。部分匯入：樂器音軌用內建合成器和鼓來播放，GarageBand for iOS 中製作的 Sampler 樂器除外。GarageBand 11 及更早版本的專案無法讀取 |
| `.blx` | `檔案 › 開啟...`，或用簡易工作流程的 `資料夾...` 匯入其資料夾 | 否 | 由 Cakewalk 或 Sonar 下載的 BandLab 專案，作為簡易工作流程的音軌，帶區域、速度、調性、混音、音量和聲像自動化以及常用效果器。部分匯入：BandLab 的樂器音色和其他踏板效果會列在匯入報告中 |
| `.mid`、`.midi` | `檔案 › 開啟...` 會建立新專案。`檔案 › 匯入 › MIDI檔案...` 和鋼琴捲軸則匯入到目前的專案 | `檔案 › 匯出 › MIDI檔案...`（`Ctrl+Shift+M`） | |
| `.mod`, `.xm`, `.s3m`, `.it` | `檔案 › 開啟...` | 否 | Tracker 模組（ProTracker、FastTracker 2、Scream Tracker 3、Impulse Tracker）。樂器會變成帶有取樣的取樣器通道，樂曲會連同速度變化變成模式片段，`檢視 › 追蹤器`可以編輯音符。部分支援：滑音、顫音和琶音等效果、封套以及正在發聲的音符上的音量變化會列在匯入報告中 |
| `.wav` | 作為取樣或音訊片段 | `檔案 › 匯出 › Wave檔案...`，16 位元、24 位元或 32 位元浮點數 | 可選的抖動、速度資訊、循環標記和播放清單標記 |
| `.mp3` | 作為取樣或音訊片段 | `檔案 › 匯出 › MP3檔案...` | 固定位元率 |
| `.ogg` | 作為取樣或音訊片段 | `檔案 › 匯出 › OGG檔案...` | |
| `.flac` | 作為取樣或音訊片段 | `檔案 › 匯出 › FLAC檔案...`，壓縮 0 到 8 | 每個等級都是無損 |
| `.aif`、`.aiff` | 作為取樣或音訊片段 | `檔案 › 匯出 › AIFF檔案...`，16 位元或 24 位元 | |
| `.m4a` | – | `檔案 › 匯出 › M4A檔案...` | 僅匯出。AAC，在 Windows 和 macOS 上使用系統的編碼器；Linux 上不提供 |
| `.wv` | 作為取樣 | 否 | WavPack 取樣，與 FL Studio 的套件中附帶的相同 |
| `.sf2`、`.sf3` | 由 LYNX Soundfont 播放 | 否 | SoundFont |
| `.dwp`、`.dwb` | 在瀏覽器中按兩下、拖放到通道機架，或使用 DirectWave 的 `開啟程式 / 音色庫...` | DirectWave 的程式和音色庫儲存 | DirectWave 程式和音色庫。取樣會在 DirectWave 尋找它們的位置尋找 |
| `.gig` | 作為 DirectWave 通道 | 否 | GigaStudio 樂器：力度層、輪流取樣、循環、調音和放大包絡。釋放觸發取樣、鍵位切換和壓縮取樣暫不讀取 |
| `.ncw` | 作為取樣或音訊片段 | 否 | Kontakt 的壓縮取樣。Kontakt 的 `.nki` 樂器需要把 Kontakt 本身作為外掛；受保護的音色庫永遠不會被解密 |
| `.fst` | 從瀏覽器開啟 | 否 | FL Studio 原生外掛的預設集。僅限 LYNX SHARE 有對應引擎的外掛 |
| `.fsc` | 鋼琴捲軸和事件編輯器 | 鋼琴捲軸和事件編輯器 | FL Studio 的樂譜和自動化檔案 |
| `.lynxpreset`、`.fxp`、`.fxb`、`.vstpreset` | 被代管外掛的預設集選單中的 `載入預設集檔案...` | `儲存預設集...` 和 `匯出預設集檔案...` | 第三方外掛的預設集。`.fxp` 和 `.fxb` 用於 VST2 外掛，`.vstpreset` 用於 VST3 外掛，而且只在外掛於 LYNX SHARE 內部執行時才可用 |
| 影片檔案 | `檔案 › 匯入 › 影片檔案...` 會把影片放到播放清單上 | 匯出視窗中的 `同時寫出帶此混音的影片` | 匯入在 Windows 和 macOS 上使用系統解碼器，在 Linux 上需要 FFmpeg。匯出需要 FFmpeg |

## 其他匯出

| 指令 | 作用 |
|---|---|
| 匯出視窗的 `檔案` 選項 | 分軌：每個混音插入軌、每個通道，或僅樂器的每個通道各一個檔案 |
| `檔案 › 匯出 › 所有 Playlist 軌道` | 每條播放清單軌道一個檔案 |
| `檔案 › 匯出 › 所有 Playlist 軌道群組` | 每個軌道群組一個檔案 |
| `檔案 › 匯出 › 母帶處理...` | 算繪專案並在你的電腦上母帶處理到響度目標 |
| `檔案 › 匯出 › 用於其他應用程式...` | 一個資料夾，內含分軌、MIDI 檔案、Ableton Live 套裝、FL Studio 專案、完整混音和 README，可依需要壓縮成 zip |
| `檔案 › 匯出 › 專案骨架...` | 把通道與效果器預設集及樂譜寫到資料夾 |

匯出視窗可以算繪歌曲、目前的樣式或播放清單的選取範圍。

## 從命令列

在 Windows 上，程式是 `LYNX.exe`。在 macOS 和 Linux 上是 `LYNX`。

```
LYNX.exe project.lynx                       open a project (any file File › Open takes)
LYNX.exe --export project.flp out.flac      render the song (wav, flac, ogg, mp3, aif; m4a on Windows and macOS)
LYNX.exe --stems project.flp base.wav       one WAV per mixer insert that has sound
LYNX.exe --export-flp project.lynx out.flp  write an FL Studio project
```

`--export`、`--stems` 和 `--export-flp` 也接受 `.als` 作為輸入。

## 須知

- 長度超過 30 秒的音訊檔案會從磁碟快取串流播放。
- 儲存時絕不會寫入 `.flp`、`.als`、`.flm`、`.dawproject`、`.band` 或 `.blx`。要用匯出指令來寫入新檔案。
- `.wv` 檔案是唯讀的。
- `.zip` 會先解壓縮到資料夾，再開啟裡面的專案。

## 相關頁面

- [匯入專案](/wiki/importing-projects/)
- [匯出](/wiki/exporting/)
- [命令列](/wiki/command-line/)
- [從 FL Studio 轉換](/wiki/coming-from-fl-studio/)
- [從 Ableton Live 轉換](/wiki/coming-from-ableton/)
