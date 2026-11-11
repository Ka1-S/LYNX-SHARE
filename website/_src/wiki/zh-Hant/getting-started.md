---
source: a6d9063ec3
---
LYNX SHARE 是一款免費、開放原始碼的 DAW，支援 Windows、macOS 和 Linux。它以樣式為核心：你在通道機架和鋼琴捲軸中編寫樣式，在播放清單中編排，在混音器中混音。它可以開啟 FL Studio 專案（`.flp`）和 Ableton Live 套裝（`.als`），並以自己的 `.lynx` 格式儲存。

它目前是 alpha 版。預期會有錯誤，請經常儲存。

## 讓它跑起來

1. 查看下面的表格，確認你的系統受到支援。
2. 下載並安裝。[安裝](/wiki/install/)頁面列出了各系統的步驟。
3. 啟動它。一個簡短的設定精靈會選擇語言、測試音訊輸出，並列出外掛資料夾，這些資料夾隨後會在背景掃描。你可以略過它，之後再用 `選項 › 設定精靈（語言、音訊、外掛資料夾）...` 重新開啟。
4. 如果聽不到聲音，請前往[音訊設定](/wiki/audio-setup/)。
5. 閱讀[視窗導覽](/wiki/tour/)，然後做出[你的第一首歌](/wiki/first-song/)。

## 需求

| 系統 | 需求 |
|---|---|
| Windows | Windows 10 或 11，64 位元 |
| macOS | macOS 11 Big Sur 或更新版本，Intel 或 Apple 晶片 |
| Linux | x86_64，glibc 2.35 或更新版本（Ubuntu 22.04+、Debian 12+、Fedora 36+、最新的 Arch 或 Manjaro），X11 或 XWayland |

你不需要音訊介面。LYNX SHARE 會透過你的系統已在使用的音效裝置播放。你也不需要 FL Studio。如果同一台電腦上裝有 FL Studio，LYNX SHARE 會找到它的取樣，並為 `.flp` 專案使用相同的外掛檔案。

第三方外掛是選用的。LYNX SHARE 有自己的樂器和效果器，列在[目錄](/instruments-and-effects/)中。

## 儲存你的作品

`Ctrl+S` 儲存，`Ctrl+Shift+S` 另存新檔，`Ctrl+Alt+S` 儲存新版本（Song_2、Song_3，依此類推）。有自動儲存，可以從 `檔案 › 開啟自動儲存資料夾` 和 `檔案 › 恢復到上一個備份...` 取得。

`檔案 › 從範本新增` 有四個起點：Empty、Basic beat、Trap with 808 和 House。

## 展示專案

Windows 的安裝程式和 zip、macOS 的 `.dmg` 以及 Linux 的 `.tar.gz` 都附有展示專案 `Demo.lynx`。用 `檔案 › 開啟...`（`Ctrl+O`）開啟它。在 Windows 上，它位於安裝資料夾中，就在 `LYNX.exe` 旁邊。

你也可以在終端機中產生它。見[命令列](/wiki/command-line/)。

```
LYNX.exe --showcase out.lynx
```

## 接下來看哪裡

| 你想要 | 閱讀 |
|---|---|
| 了解各個視窗 | [通道機架](/wiki/channel-rack/)、[鋼琴捲軸](/wiki/piano-roll/)、[播放清單](/wiki/playlist/)、[混音器](/wiki/mixer/)、[瀏覽器](/wiki/browser/) |
| 開啟其他程式的專案 | [從 FL Studio 轉換](/wiki/coming-from-fl-studio/)、[從 Ableton Live 轉換](/wiki/coming-from-ableton/) |
| 使用你的 VST、CLAP、LV2 或 AU 外掛 | [外掛](/wiki/plugins/) |
| 查詢按鍵 | [快速鍵](/wiki/shortcuts/) |
| 解決問題 | [疑難排解](/wiki/troubleshooting/)和 [FAQ](/faq/) |

## 須知

- 程式檔案在 Windows 上叫 `LYNX.exe`，在 macOS 和 Linux 上叫 `LYNX`。設定資料夾也叫 `LYNX`。
- 在 Windows 和 macOS 上，第一次啟動時可能會跳出系統警告，因為 Windows 版還沒有程式碼簽章，macOS 版也還沒有公證。[安裝](/wiki/install/)頁面說明該點哪裡。
- 如果系統的語言是 LYNX SHARE 沒有的，介面預設為英文。你可以在 `選項 › 一般設定...` 中更改。新的語言會在重新啟動後生效。
- FL Studio 專案中的 FLEX 包是加密的，不會被開啟。這些通道會得到依預設集名稱選出的合成器音色。

## 相關頁面

- [安裝](/wiki/install/)
- [音訊設定](/wiki/audio-setup/)
- [視窗導覽](/wiki/tour/)
- [你的第一首歌](/wiki/first-song/)
- [下載](/download/)
