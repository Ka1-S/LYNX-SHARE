---
source: 8a120ab7de
---
每個版本都在[發行頁面](https://github.com/Ka1-S/LYNX-SHARE/releases)提供 Windows、macOS 和 Linux 的建置版本。這些檔案由 GitHub Actions 從標記的原始碼建置，並連同 SHA-256 列在 `SHA256SUMS.txt` 中。[下載](/download/)頁面連結到目前的版本。

## 在 Windows 上安裝

Windows 10 或 11，64 位元。

1. 下載 `LYNX-<version>-setup-x64.exe`。
2. 執行它。它只為你的使用者帳號安裝，不需要管理員權限。
3. SmartScreen 可能會發出警告，因為這個可執行檔還沒有程式碼簽章。請選擇 `More info`，再選擇 `Run anyway`。
4. 從開始功能表或桌面捷徑啟動 LYNX SHARE。

安裝程式會讓 LYNX SHARE 成為開啟 `.lynx` 專案的程式。對於 `.flp` 檔案，它會在 *Open with* 下提供 LYNX SHARE，而不會更改 FL Studio 的預設設定。

還有另外兩種方式：

| 方式 | 檔案或位置 | 作用 |
|---|---|---|
| 可攜式 zip | `LYNX-<version>-windows-x64.zip` | 解壓縮到任何地方，執行 `LYNX.exe`。不會安裝任何東西。 |
| Microsoft Store | [Store 上的 LYNX SHARE](https://apps.microsoft.com/detail/9NQRFBFDCX6N) | 由 Store 安裝並保持更新。 |

## 在 macOS 上安裝

macOS 11 Big Sur 或更新版本。一個通用的 `LYNX-<version>-macos-universal.dmg` 同時涵蓋 Intel 和 Apple 晶片。

1. 開啟 `.dmg`，把 LYNX SHARE 拖到 *Applications*。
2. 這個應用程式沒有經過 Apple 公證，也沒有 Developer ID 簽章，所以第一次啟動需要多一個步驟。在 *Applications* 中對 LYNX SHARE 按右鍵（或按住 Control 點擊），選擇 *Open*，然後再選一次 *Open*。
3. 在 macOS 15 Sequoia 或更新版本上，先嘗試開啟一次，然後前往 *System Settings › Privacy & Security*，點擊 *Open Anyway*。

用終端機的替代方法：

```
xattr -dr com.apple.quarantine /Applications/LYNX.app
```

第一次錄音時，macOS 會要求麥克風存取權限。

## 在 Linux 上安裝

x86_64，glibc 2.35 或更新版本（Ubuntu 22.04+、Debian 12+、Fedora 36+、最新的 Arch 或 Manjaro）。LYNX SHARE 需要 X11 或 XWayland。音訊透過 PipeWire、JACK 或 ALSA 輸出，為此不需要額外安裝任何東西。

AppImage（`LYNX-<version>-x86_64.AppImage`）：

```
chmod +x LYNX-*.AppImage
./LYNX-*.AppImage
```

有些發行版需要 libfuse2 才能執行 AppImage（Ubuntu 22.04 上是 `libfuse2`，24.04 上是 `libfuse2t64`，Arch 上是 `fuse2`，Fedora 上是 `fuse-libs`）。第一次啟動時，設定精靈會詢問是否把 LYNX SHARE 加入應用程式選單，並用它開啟 `.lynx` 檔案。你之後可以在 `選項 › 一般設定...` 中切換，也可以在終端機中用 `./LYNX-*.AppImage --menu-entry on`（或 `關`）切換。

如果你不想安裝 libfuse2，請使用 `LYNX-<version>-linux-x86_64.tar.gz`：解壓縮後執行 `./install.sh`。這會把 LYNX SHARE 安裝到你使用者目錄的 `~/.local` 下，並附帶選單項目、圖示和 `.lynx` 檔案類型。

## 設定存放在哪裡

| 系統 | 設定資料夾 |
|---|---|
| Windows | `%APPDATA%\LYNX` |
| macOS | `~/Library/Application Support/LYNX` |
| Linux | `~/.config/LYNX`（或在 `$XDG_CONFIG_HOME` 下） |

這個資料夾存放你的選項、選擇的語言（`language.txt`）、你的快速鍵（`keys.json`）、你的外掛資料夾（`plugin-folders.txt`）和已掃描外掛的清單（`plugins.xml`）。

## 更新

啟動時，LYNX SHARE 會向 GitHub 詢問最新的版本號。不會傳送任何關於你的資料。有新版本時，會出現一個列，顯示 `LYNX SHARE <version> is available`，並帶有 `新功能` 按鈕和更新按鈕。`說明 › 檢查更新...` 可隨時查詢。

| 版本 | 更新方式 |
|---|---|
| Windows 安裝程式 | `更新` 會下載新的安裝程式，詢問未儲存的變更，然後安裝並重新啟動 |
| Windows 可攜式 zip、macOS | `更新` 會下載新檔案，並在其資料夾中顯示 |
| Linux AppImage | `更新` 會就地更新 AppImage 檔案。Gear Lever 和 AppImageUpdate 也能從外部做到同樣的事 |
| Linux `.tar.gz` | `下載...` 會開啟下載頁面 |
| Microsoft Store | 由 Store 更新。這個版本沒有自己的更新檢查 |

若要關閉檢查，請開啟 `選項 › 一般設定...`，取消勾選 `自動檢查更新`。

## 移除

- Windows 安裝程式：從 Windows 的 *應用程式與功能* 清單中解除安裝 LYNX SHARE。
- 可攜式 zip：刪除你解壓縮出來的資料夾。
- Linux `.tar.gz`：執行 `./install.sh --uninstall`。

## 須知

- 在專案的程式碼簽章申請通過之前，Windows 版本都沒有簽章。macOS 應用程式只有臨時簽章。Linux 套件沒有簽章。
- 若要驗證下載的檔案，請把它的 SHA-256 與發行頁面上 `SHA256SUMS.txt` 中對應的那一行比對。
- 安裝程式、zip、`.dmg` 和 `.tar.gz` 都附有展示專案 `Demo.lynx`。
- 用關閉按鈕關閉更新列，會略過該版本。

## 相關頁面

- [從這裡開始](/wiki/getting-started/)
- [音訊設定](/wiki/audio-setup/)
- [疑難排解](/wiki/troubleshooting/)
- [下載](/download/)
- [更新](/updates/)
