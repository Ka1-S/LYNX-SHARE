---
source: a3748d4a2a
---
下面每個標題都是一個問題。如果你的問題不在其中，請依文末的說明回報。

## 啟動程式

### Windows：第一次啟動時 SmartScreen 發出警告

這個可執行檔還沒有程式碼簽章，所以第一次執行時 SmartScreen 可能會發出警告。

1. 點擊 `More info`。
2. 點擊 `Run anyway`。

安裝程式不需要管理員權限。

### macOS：「無法開啟」

這個應用程式沒有經過 Apple 公證，也沒有 Developer ID 簽章，所以第一次啟動需要多一個步驟。

1. 把 LYNX SHARE 拖到 `Applications`。
2. 對它按右鍵（Control 點擊），選擇 `開啟`。
3. 再點擊一次 `開啟`。

在 macOS 15 Sequoia 或更新版本上，先嘗試開啟一次，然後前往 `System Settings › Privacy & Security`，點擊 `Open Anyway`。或者在「終端機」中輸入：

```
xattr -dr com.apple.quarantine /Applications/LYNX.app
```

第一次錄音時，macOS 會要求麥克風存取權限。

### Linux：AppImage 無法啟動

LYNX SHARE 需要 x86_64 系統、glibc 2.35 或更新版本（Ubuntu 22.04、Debian 12、Fedora 36、最新的 Arch 或 Manjaro），以及 X11 或 XWayland。

1. 用下面的兩個指令，讓檔案可執行並執行它。
2. 如果仍然無法啟動，請安裝 libfuse2。套件在 Ubuntu 22.04 上是 `libfuse2`，在 Ubuntu 24.04 上是 `libfuse2t64`，在 Arch 上是 `fuse2`，在 Fedora 上是 `fuse-libs`。
3. 如果你不想安裝它，請改為下載 `.tar.gz`，解壓縮後執行 `./install.sh`。

```
chmod +x LYNX-*.AppImage
./LYNX-*.AppImage
```

`./install.sh` 會把 LYNX SHARE 安裝到你使用者目錄的 `~/.local` 下，並附帶選單項目。`./install.sh --uninstall` 可將其移除。

## 聲音

### 沒有聲音，或音訊裝置不對

1. 開啟 `選項 › 音訊設定...`。
2. 選擇驅動程式。
3. 選擇輸出裝置。

| 系統 | 驅動程式 |
|---|---|
| Windows | Windows Audio (WASAPI)、DirectSound、ASIO 和內建的 LYNX ASIO 驅動程式 |
| macOS | Core Audio |
| Linux | PipeWire（原生）、JACK 和 ALSA。第一次啟動時，LYNX SHARE 會開啟其中第一個有裝置的 |

ASIO 是 Windows 的驅動程式模型，只存在於 Windows。`選項 › 設定精靈（語言、音訊、外掛資料夾）...` 也會測試音訊裝置。見[音訊設定](/wiki/audio-setup/)。

### Linux：聲音斷斷續續

音訊執行緒應以即時優先權執行。

1. 開啟 `選項 › 音訊設定...` 並查看 `狀態`。它會顯示執行緒是否取得了即時優先權。
2. 如果沒有，請安裝 rtkit（`sudo apt install rtkit`、`sudo dnf install rtkit` 或 `sudo pacman -S rtkit`），或者像 JACK 的設定那樣，把自己加入帶有即時限制的 `音訊` 群組。
3. 登出後再登入。

詳細資訊見 [LINUX.md](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/LINUX.md)。

## 外掛

### 外掛當掉

讓它在自己的處理程序中執行，這樣只有該外掛會停止。

1. 開啟 `選項 › 一般設定...`。
2. 開啟 `在獨立處理程序中執行外掛（防止外掛崩潰影響LYNX SHARE）`。
3. 重新開啟專案。

在自己處理程序中當掉或停止回應的外掛會被靜音。請儲存專案並重新開啟，才能再次載入該外掛。見[第三方外掛](/wiki/plugins/)。

### 外掛不在清單中

1. 選擇 `選項 › 掃描外掛`。
2. 如果仍然找不到，請開啟 `選項 › 外掛管理器...`，點擊 `外掛資料夾...`，加入外掛所在的資料夾。
3. 查看 `失敗` 分頁。掃描失敗的外掛檔案會被略過。在 Windows 上，原因記錄在 `%APPDATA%\LYNX\scan-failures.log` 中。
4. 點擊 `全部重新掃描...`，重試失敗的檔案。

### 專案用到未安裝的外掛

效果器會被角色相同的 LYNX SHARE 效果器取代，其設定仍保留在專案中。`說明 › 缺失的檔案...` 會列出缺少的內容。見[內建樂器與效果器](/wiki/built-in/)。

### 影片在 Linux 上無法使用

影片軌道和 `Export with video` 需要 FFmpeg。LYNX SHARE 會告訴你如何安裝。

## 崩潰報告

![下次啟動時顯示的崩潰報告詢問，有 Send、Show what will be sent 和 Don't send](/assets/img/wiki/crash-report.webp)

崩潰報告需要你主動同意。崩潰或停止回應之後，下次啟動時會詢問是否傳送報告。

| 指令 | 作用 |
|---|---|
| `顯示將要傳送的內容` | 顯示確切的文字 |
| `改為在GitHub上報告` | 開啟 GitHub 的問題表單，並預先填好內容。檔案需由你自己附上 |
| `開啟報告所在的資料夾` | 在你的電腦上顯示報告檔案 |
| `Include file names (projects, samples)` | 關閉時，每個檔案只傳送副檔名 |
| `始終傳送，不再詢問` | 之後的報告不再詢問，直接傳送 |

`選項 › 一般設定...` 中的 `崩潰報告` 可設為 `Ask`（預設）、`始終傳送` 或 `從不傳送`。

報告包含 LYNX SHARE 版本、作業系統、CPU、顯示卡及其驅動程式、螢幕尺寸、音訊驅動程式和裝置、崩潰或停止回應檔案、工作階段的最後 60 個步驟，以及已載入的第三方外掛的名稱和版本。

報告不包含音訊、專案、取樣、你的電腦名稱或使用者名稱。伺服器不儲存 IP 位址。已傳送的 id 列在設定資料夾的 `crash-reports\sent.log` 中。若要刪除某份報告，請附上它的 id 開一個問題。完整文字見[隱私權政策](https://github.com/Ka1-S/LYNX-SHARE/blob/main/PRIVACY.md)。

## 報告問題

![「報告問題」視窗：說明框、選填的電子郵件地址和 Send 按鈕](/assets/img/wiki/report-problem.webp)

1. 選擇 `說明 › 報告問題...`。
2. 描述發生了什麼事。電子郵件地址是選填的，想收到回覆時再填。
3. 如果問題是崩潰，請保持勾選 `附上最新的崩潰/停止回應檔案和本次工作階段的步驟`。
4. 點擊 `顯示將要傳送的內容` 檢查文字。
5. 點擊 `傳送`。在此之前不會傳送任何東西。

你也可以到 [github.com/Ka1-S/LYNX-SHARE/issues](https://github.com/Ka1-S/LYNX-SHARE/issues) 開一個問題。

## 須知

- 崩潰之後，專案的備份副本在 LYNX SHARE 的自動儲存資料夾中。
- 如果崩潰與某個外掛有關，詢問視窗會提供從現在起讓該外掛在獨立處理程序中執行的選項。
- 設定資料夾在 Windows 上是 `%APPDATA%\LYNX`，在 macOS 上是 `~/Library/Application Support/LYNX`，在 Linux 上是 `~/.config/LYNX`。

## 相關頁面

- [音訊設定](/wiki/audio-setup/)
- [第三方外掛](/wiki/plugins/)
- [安裝](/wiki/install/)
- [FAQ](/faq/)
- [崩潰防護](/why/#crash)
