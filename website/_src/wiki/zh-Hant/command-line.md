---
source: 98829adcb8
---
LYNX SHARE 也可以不開啟視窗直接執行。有一個選項能把專案渲染成音訊，有些能把它轉成 FL Studio 專案或 Ableton Live 套裝，另外還有幾個用於測試。專案自己的檢查也使用這些選項。

程式在 Windows 上是 `LYNX.exe`，在 macOS 和 Linux 上是 `LYNX`。在 macOS 上完整路徑是 `/Applications/LYNX.app/Contents/MacOS/LYNX`。各系統的選項都相同。

## 把專案渲染成檔案

1. 在程式所在的資料夾開啟終端機。在 Windows 上，這是安裝資料夾，或你解壓縮 zip 的資料夾。
2. 執行 `LYNX.exe --export project.flp out.flac`，換成你自己的專案和輸出檔案。
3. 等程式結束。不會開啟任何視窗。
4. 讀取寫在輸出檔旁邊的 `out.flac.txt`。對於匯入的專案，它會列出哪些已轉換、哪些是近似處理、哪些被省略，以及哪些外掛找到了或缺失。

## 開啟專案

```
LYNX.exe project.lynx
```

啟動 LYNX SHARE 並顯示視窗，然後開啟該檔案。它接受 `.lynx`、`.flp`、`.als`、`.flm` 和 `.mid`。相對路徑以目前資料夾為準。

## 日常使用的選項

| 選項 | 作用 |
|---|---|
| `--export <project> <out>` | 渲染整首歌，包含尾音，與匯出視窗相同。`<out>` 的副檔名決定格式：`.wav`（24 位元）、`.flac`、`.ogg` 或 `.mp3`（320 kbps） |
| `--export ... --pattern` | 只渲染目前樣式 |
| `--export ... --insert N` | 只渲染混音插入軌 N 的輸出 |
| `--export ... --rate N` | 以另一個取樣率渲染，單位為 Hz |
| `--stems <project> <base.wav>` | 為每個有通道的混音插入軌寫出一個 WAV：`base - 1.wav`、`base - 2.wav`，依此類推 |
| `--export-flp <project> <out.flp>` | 寫出 FL Studio 專案。若為 `out.zip`，則寫出壓縮的循環素材包 |
| `--export-flp ... --substitute` | 把僅限 LYNX 的外掛換成最接近的 FL 外掛，而不是把它們渲染成音訊 |
| `--export-als <project> <folder>` | 把 Ableton Live 套裝寫到 `<folder>/<name> Project/<name>.als`，適用於 Live 12 |
| `--export-als ... --live11` | 寫出適用於 Live 11 的套裝 |
| `--devices <report.txt>` | 列出這台電腦的音訊驅動程式類型和裝置 |
| `--showcase <out.lynx>` | 寫出 LYNX SHARE 自己的展示專案 |
| `--lang <id>` | 這次執行使用另一種介面語言，例如 `--lang pt-BR` |
| `--menu-entry on` 或 `關` | 僅限 Linux AppImage：把 LYNX SHARE 加入應用程式選單，或將其移除 |

`--export`、`--stems` 和 `--export-flp` 的專案可以是 `.lynx`、`.flp` 或 `.als`。`--export-als` 也接受 `.flm`。

範例：

```
LYNX.exe --export song.lynx song.wav
LYNX.exe --export song.lynx drums.wav --insert 1
LYNX.exe --stems song.lynx song.wav
LYNX.exe --export-flp song.lynx song.flp
LYNX.exe --export-als song.lynx out-folder --live11
LYNX.exe --devices devices.txt
LYNX --lang de
```

## 用於測試的選項

這些選項用來檢查 LYNX SHARE 本身。提交錯誤報告時很有用。

| 選項 | 作用 |
|---|---|
| `--selftest <report.txt> [demo.lynx]` | 不使用音訊裝置執行自我測試。通過時，報告以 `result=OK` 結尾 |
| `--snapshot <prefix> [project]` | 把介面及其視窗繪製成以該前綴命名的 PNG 檔案。這份 wiki 裡的圖片就是這樣做出來的 |
| `--render <project> <out.wav> <seconds>` | 把目前樣式渲染指定秒數，存成 24 位元 WAV |
| `--plugintest <plugin> <report.txt>` | 載入並測試單一 VST3 或 CLAP 外掛 |
| `--sandboxtest <plugin> <report.txt>` | 在獨立的處理程序中測試外掛，包括會當掉的外掛 |
| `--scantest <folder> <report.txt>` | 在獨立的處理程序中掃描資料夾裡的外掛 |
| `--fldb <report.txt> [project.flp]` | 回報在此找到的 FL Studio 安裝、它的外掛資料庫，以及專案的外掛比對結果 |
| `--devicetest <type> <device> <report.txt>` | 開啟音訊驅動程式播放一秒鐘的靜音，並回報結果 |
| `--uiperf <project> <report.txt>` | 為混音器、播放清單和通道機架的繪製計時 |

範例：

```
LYNX.exe --selftest report.txt
LYNX.exe --snapshot shots/s project.lynx
LYNX --devicetest PipeWire "System default" out.txt
```

## 須知

- 結果會寫入檔案，不會顯示在終端機。`--export` 寫出 `<out>.txt`，`--export-flp` 寫出 `<out.flp>.txt`，`--export-als` 在資料夾中寫出 `export-als.txt`，`--stems` 寫出 `<base.wav>.txt`。
- 程式會讀取標準輸入。在 shell 迴圈中，請在每次呼叫後加上 `< /dev/null`。
- `--snapshot` 預設以 1280x720 繪製，除非環境變數 `LYNX_SNAPSHOT_SIZE` 另有指定，例如 `1920x1080`。
- `--lang` 接受 `選項 › 一般設定...` 中所列語言的 id，例如 `en`、`pt-BR`、`de`、`es`、`fr` 或 `ja`。不認得的 id 會被忽略。
- `--export` 使用與匯出視窗相同的渲染流程，但不含視窗的其他選項。若需要抖動、位元深度或正規化，請使用視窗。
- 原始碼裡還有更多測試選項。它們屬於內部用途，不同版本之間可能會改變。

## 相關頁面

- [匯出](/wiki/exporting/)
- [檔案格式](/wiki/file-formats/)
- [從原始碼建置](/wiki/build-from-source/)
- [疑難排解](/wiki/troubleshooting/)
