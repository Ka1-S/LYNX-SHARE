---
source: edc71061e6
---
LYNX SHAREはC++20とJUCE 8で書かれ、CMakeでビルドします。ソースは [github.com/Ka1-S/LYNX-SHARE](https://github.com/Ka1-S/LYNX-SHARE) にあり、GNU Affero General Public License v3.0で公開されています。リリースファイルは、タグを付けたソースからGitHub Actionsがビルドしています。このページが必要なのは、コードを変更したり、自分でビルドしたりする場合だけです。

## Linuxでビルドする

1. 下に挙げた依存パッケージをインストールします。
2. `git clone https://github.com/Ka1-S/LYNX-SHARE.git lynx` でソースを取得し、`lynx` フォルダーに移動します。
3. `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release` で構成します。
4. `cmake --build build` でビルドします。

Ubuntu 24.04での依存パッケージは次のとおりです。

```
sudo apt install build-essential cmake ninja-build pkg-config libasound2-dev libjack-jackd2-dev libx11-dev \
  libxrandr-dev libxinerama-dev libxcursor-dev libxext-dev libxcomposite-dev libfreetype-dev libfontconfig1-dev libgl-dev
```

## Visual StudioでWindows向けにビルドする

「Desktop development with C++」を含むVisual Studio 2022 Build Tools、CMake 3.22以降、Gitが必要です。

```
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

## WSLまたはLinuxからWindows向けにビルドする

プロジェクト自体はこの方法で開発されています。Windows版のプログラムを、Visual Studioなしで、Zigでクロスコンパイルします。

```
./scripts/build-windows-zig.sh
```

出力は `build-win/LYNX.exe` です。

| 要件 | バージョン | 既定の場所 |
|---|---|---|
| Zig | 0.14.1 | `~/opt/zig-x86_64-linux-0.14.1` |
| JUCE | 8.0.4 | `~/opt/juce-8.0.4` |

JUCEは次のコマンドで取得できます。

```
git clone --depth 1 -b 8.0.4 https://github.com/juce-framework/JUCE ~/opt/juce-8.0.4
```

パスが異なる場合は、環境変数 `ZIG` と `JUCE_DIR` を設定します。

## macOSでビルドする

Xcodeのコマンドラインツール (`xcode-select --install`)、CMake、Ninjaが必要です。たとえば `brew install cmake ninja` で入れられます。

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build
```

`CMAKE_OSX_ARCHITECTURES="arm64;x86_64"` を指定すると、ユニバーサルアプリが作られます。現在のMac向けだけにビルドするなら、これを省きます。

## コアのテストを実行する

コア (モデル、シーケンサー、インストゥルメント、エフェクト、`.flp` のインポート) は素のC++で、JUCEなしで、C++20対応のコンパイラーならテストできます。

```
./scripts/build-local.sh ["folder with .flp files"]
```

実行は `N checks, 0 failures` で終わるはずです。FL Studio 2026のインストール先にある `Data` フォルダーを渡すと、FL Studioに付属する `.flp` プロジェクトも解析します。このスクリプトは小さなツールを2つ作ります。`.flp` をダンプする `build-local/flpdump` と、`build-local/renderwav` です。

ビルド後は、オーディオデバイスがなくてもプログラムが自己チェックできます。

```
LYNX.exe --selftest report.txt
```

レポートは `result=OK` で終わります。ほかのオプションは[コマンドライン](/wiki/command-line/)のページにあります。

## ソースツリー

| フォルダー | 内容 |
|---|---|
| `src/core/` | モデル、シーケンサー、インストゥルメント、エフェクト、`.flp` のインポート。素のC++で、JUCEなしでテストできます |
| `src/flp/` | `.flp` 形式のリーダー |
| `src/engine/` | オーディオエンジン: デバイス、グラフ、サンプル、プラグイン、プロジェクトファイル |
| `src/ui/` | インターフェース: チャンネルラック、ピアノロール、プレイリスト、ミキサー、各ウィンドウ |
| `third_party/` | CLAPとWavPackデコーダー |
| `tests/` | コアのテスト |
| `tools/` | `flpdump`、`renderwav`、`comparewav`、`make_logo` |
| `docs/` | アーキテクチャ、状況、ロードマップ、FL Studioの挙動の分析 |

## 知っておくと便利なこと

- JUCE 8はMinGWを公式にはサポートしていません。Zigのビルドスクリプトが、その違いを回避しています。修正は `cmake/mingw/` と `src/platform/MingwFixes.cpp` にあります。
- `build-windows-zig.sh` は一度に1つだけ実行してください。Zigのビルドを同時に2つ走らせると、止まることがあります。別々のワークツリーで並列にビルドするときは、`ZIG_GLOBAL_CACHE_DIR` と `ZIG_LOCAL_CACHE_DIR` でそれぞれ専用のキャッシュを指定します。
- 一部のエンジンは、内蔵の音やテーブルを、お使いのFL Studioのインストール先から読み込みます。FL Studioのものはリポジトリに含まれていません。
- `docs/` のドキュメントの一部は、まだポルトガル語です。翻訳を進めています。
- 改変したバージョンを配布する場合は、ネットワークサービスとして提供する場合も含め、ライセンスにより、同じライセンスでそのソースを公開する必要があります。

## 関連ページ

- [開発環境の全体 (SETUP_DEV.md)](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/SETUP_DEV.md)
- [LinuxでのLYNX SHARE (LINUX.md)](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/LINUX.md)
- [リポジトリとそのREADME](https://github.com/Ka1-S/LYNX-SHARE)
- [コマンドライン](/wiki/command-line/)
- [ファイル形式](/wiki/file-formats/)
