---
source: edc71061e6
---
LYNX SHARE 以 C++20 搭配 JUCE 8 撰寫，並用 CMake 建置。原始碼在 [github.com/Ka1-S/LYNX-SHARE](https://github.com/Ka1-S/LYNX-SHARE)，採用 GNU Affero General Public License v3.0 授權。發行檔由 GitHub Actions 從帶標籤的原始碼建置，所以只有在你想修改程式碼或自行建置時才需要這一頁。

## 在 Linux 上建置

1. 安裝下方列出的相依套件。
2. 用 `git clone https://github.com/Ka1-S/LYNX-SHARE.git lynx` 取得原始碼，然後進入 `lynx` 資料夾。
3. 用 `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release` 進行設定。
4. 用 `cmake --build build` 建置。

Ubuntu 24.04 上的相依套件：

```
sudo apt install build-essential cmake ninja-build pkg-config libasound2-dev libjack-jackd2-dev libx11-dev \
  libxrandr-dev libxinerama-dev libxcursor-dev libxext-dev libxcomposite-dev libfreetype-dev libfontconfig1-dev libgl-dev
```

## 在 Windows 上用 Visual Studio 建置

需要安裝了「使用 C++ 的桌面開發」的 Visual Studio 2022 Build Tools、CMake 3.22 或更新版本，以及 Git。

```
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

## 從 WSL 或 Linux 建置 Windows 版

專案本身就是這樣開發的：Windows 程式用 Zig 交叉編譯，不需要 Visual Studio。

```
./scripts/build-windows-zig.sh
```

結果是 `build-win/LYNX.exe`。

| 需求 | 版本 | 預設位置 |
|---|---|---|
| Zig | 0.14.1 | `~/opt/zig-x86_64-linux-0.14.1` |
| JUCE | 8.0.4 | `~/opt/juce-8.0.4` |

JUCE 可以這樣取得：

```
git clone --depth 1 -b 8.0.4 https://github.com/juce-framework/JUCE ~/opt/juce-8.0.4
```

如果你的路徑不同，請設定環境變數 `ZIG` 和 `JUCE_DIR`。

## 在 macOS 上建置

需要 Xcode 命令列工具（`xcode-select --install`）、CMake 和 Ninja，例如可透過 `brew install cmake ninja` 安裝。

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build
```

`CMAKE_OSX_ARCHITECTURES="arm64;x86_64"` 會產生通用應用程式。省略它則只為目前這台 Mac 建置。

## 執行核心測試

核心（模型、音序器、樂器、效果器和 `.flp` 匯入）是純 C++，不需要 JUCE，用任何 C++20 編譯器都能測試。

```
./scripts/build-local.sh ["folder with .flp files"]
```

執行結果必須以 `N checks, 0 failures` 結尾。如果指定 FL Studio 2026 安裝位置的 `Data` 資料夾，它還會解析隨 FL Studio 附帶的 `.flp` 專案。這個腳本也會建置兩個小工具：`build-local/flpdump`（傾印 `.flp`）和 `build-local/renderwav`。

建置之後，程式不需要音訊裝置就能自我檢查：

```
LYNX.exe --selftest report.txt
```

報告結尾會是 `result=OK`。更多選項在[命令列](/wiki/command-line/)頁面。

## 原始碼樹

| 資料夾 | 內容 |
|---|---|
| `src/core/` | 模型、音序器、樂器、效果器和 `.flp` 匯入。純 C++，不需要 JUCE 即可測試 |
| `src/flp/` | `.flp` 格式讀取器 |
| `src/engine/` | 音訊引擎：裝置、圖、取樣、外掛、專案檔案 |
| `src/ui/` | 介面：通道機架、鋼琴捲軸、播放清單、混音器、視窗 |
| `third_party/` | CLAP 和 WavPack 解碼器 |
| `tests/` | 核心測試 |
| `tools/` | `flpdump`、`renderwav`、`comparewav`、`make_logo` |
| `docs/` | 架構、狀態、路線圖，以及對 FL Studio 行為的分析 |

## 須知

- JUCE 8 並未正式支援 MinGW。Zig 建置腳本會處理這些差異。修正內容在 `cmake/mingw/` 和 `src/platform/MingwFixes.cpp`。
- 一次只能執行一個 `build-windows-zig.sh`。同時跑兩個 Zig 建置可能會卡住。若要在不同的工作樹中平行建置，請用 `ZIG_GLOBAL_CACHE_DIR` 和 `ZIG_LOCAL_CACHE_DIR` 為每個建置指定各自的快取。
- 有些引擎會從你自己的 FL Studio 安裝讀取內建音色和表格。儲存庫中沒有任何來自 FL Studio 的內容。
- `docs/` 中有一部分文件仍是葡萄牙文，正在翻譯中。
- 如果你散布修改過的版本，包括作為網路服務提供，授權條款要求你以相同授權公開它的原始碼。

## 相關頁面

- [完整開發環境（SETUP_DEV.md）](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/SETUP_DEV.md)
- [Linux 上的 LYNX SHARE（LINUX.md）](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/LINUX.md)
- [儲存庫及其 README](https://github.com/Ka1-S/LYNX-SHARE)
- [命令列](/wiki/command-line/)
- [檔案格式](/wiki/file-formats/)
