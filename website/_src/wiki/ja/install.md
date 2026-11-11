---
source: 8a120ab7de
---
各リリースには、Windows、macOS、Linux向けのビルドが[リリースページ](https://github.com/Ka1-S/LYNX-SHARE/releases)に用意されています。ファイルは、タグ付けされたソースからGitHub Actionsでビルドされ、SHA-256が `SHA256SUMS.txt` に載っています。[ダウンロード](/download/)ページから、最新のファイルにリンクしています。

## Windowsにインストールする

Windows 10または11、64ビット。

1. `LYNX-<version>-setup-x64.exe` をダウンロードします。
2. 実行します。現在のユーザー用にだけインストールされ、管理者権限は不要です。
3. 実行ファイルにはまだコード署名がないため、SmartScreenが警告を出すことがあります。`More info` を選び、続けて `Run anyway` を選びます。
4. スタートメニューか、デスクトップのショートカットからLYNX SHAREを起動します。

インストーラーは、LYNX SHAREを `.lynx` プロジェクトを開くプログラムとして登録します。`.flp` ファイルについては、FL Studioの既定の設定は変えずに、*プログラムから開く* の候補にLYNX SHAREを加えます。

ほかに2つの方法があります。

| 方法 | ファイルまたは場所 | 働き |
|---|---|---|
| ポータブルzip | `LYNX-<version>-windows-x64.zip` | 好きな場所に展開して、`LYNX.exe` を実行します。何もインストールされません。 |
| Microsoft Store | [StoreのLYNX SHARE](https://apps.microsoft.com/detail/9NQRFBFDCX6N) | Storeがインストールし、更新も続けます。 |

## macOSにインストールする

macOS 11 Big Sur以降。ユニバーサルの `LYNX-<version>-macos-universal.dmg` 1つで、IntelとApple siliconの両方に対応します。

1. `.dmg` を開いて、LYNX SHAREを *アプリケーション* にドラッグします。
2. このアプリはAppleによる公証を受けておらず、Developer IDの署名もないため、初回起動には余分な手順が必要です。 *アプリケーション* のLYNX SHAREを右クリック (またはControlキーを押しながらクリック) し、 *開く* を選び、もう一度 *開く* を選びます。
3. macOS 15 Sequoia以降では、一度開こうとしてから、 *システム設定 › プライバシーとセキュリティ* へ進み、 *このまま開く* をクリックします。

ターミナルを使う方法:

```
xattr -dr com.apple.quarantine /Applications/LYNX.app
```

初めて録音するときに、macOSがマイクへのアクセスを尋ねます。

## Linuxにインストールする

glibc 2.35以降のx86_64 (Ubuntu 22.04以降、Debian 12以降、Fedora 36以降、最新のArchまたはManjaro)。LYNX SHAREにはX11またはXWaylandが必要です。オーディオはPipeWire、JACK、ALSAのいずれかを通り、そのために追加でインストールするものはありません。

AppImage (`LYNX-<version>-x86_64.AppImage`):

```
chmod +x LYNX-*.AppImage
./LYNX-*.AppImage
```

ディストリビューションによっては、AppImageの実行にlibfuse2が必要です (Ubuntu 22.04では `libfuse2`、24.04では `libfuse2t64`、Archでは `fuse2`、Fedoraでは `fuse-libs`)。初回起動時にセットアップウィザードが、LYNX SHAREをアプリケーションメニューに加えるかどうか、`.lynx` ファイルをLYNX SHAREで開くようにするかどうかを尋ねます。あとから `オプション › 全般設定...` で切り替えられ、ターミナルから `./LYNX-*.AppImage --menu-entry on` (または `オフ`) でも切り替えられます。

libfuse2をインストールしたくない場合は、`LYNX-<version>-linux-x86_64.tar.gz` を使います。展開して `./install.sh` を実行してください。メニュー項目、アイコン、`.lynx` ファイルタイプとともに、`~/.local` の下に現在のユーザー用としてLYNX SHAREがインストールされます。

## 設定の保存場所

| システム | 設定フォルダー |
|---|---|
| Windows | `%APPDATA%\LYNX` |
| macOS | `~/Library/Application Support/LYNX` |
| Linux | `~/.config/LYNX` (または `$XDG_CONFIG_HOME` の下) |

このフォルダーには、オプション、選んだ言語 (`language.txt`)、ショートカット (`keys.json`)、プラグインフォルダー (`plugin-folders.txt`)、スキャン済みプラグインの一覧 (`plugins.xml`) が入っています。

## アップデート

起動時に、LYNX SHAREはGitHubに最新のバージョン番号を尋ねます。あなたに関する情報は何も送られません。新しいバージョンが出ると、バーに `LYNX SHARE <version> is available` と表示され、`新機能` ボタンとアップデートのボタンが付きます。`ヘルプ › アップデートを確認...` で、いつでも確認できます。

| ビルド | 更新の方法 |
|---|---|
| Windowsのインストーラー | `アップデート` が新しいインストーラーをダウンロードし、未保存の変更について尋ねたあと、インストールして再起動する |
| Windowsのポータブルzip、macOS | `アップデート` が新しいファイルをダウンロードし、そのフォルダーを表示する |
| Linux AppImage | `アップデート` がAppImageファイルをその場で更新する。Gear LeverやAppImageUpdateでも、外部から同じことができる |
| Linux `.tar.gz` | `ダウンロード...` でダウンロードページを開く |
| Microsoft Store | Storeが更新する。このビルドには独自の更新確認がない |

確認をオフにするには、`オプション › 全般設定...` を開いて、`アップデートを自動で確認` のチェックを外します。

## 削除する

- Windowsのインストーラー版: Windowsの *アプリと機能* の一覧からLYNX SHAREをアンインストールします。
- ポータブルzip: 展開したフォルダーを削除します。
- Linux `.tar.gz`: `./install.sh --uninstall` を実行します。

## 知っておくと便利なこと

- Windowsのビルドは、プロジェクトのコード署名の申請が承認されるまで、署名がありません。macOSのアプリには、アドホック署名しかありません。Linuxのパッケージは署名されていません。
- ダウンロードを検証するには、そのSHA-256を、リリースページの `SHA256SUMS.txt` の該当行と比べてください。
- インストーラー、zip、`.dmg`、`.tar.gz` には、デモプロジェクトの `Demo.lynx` が入っています。
- アップデートバーを閉じるボタンで閉じると、そのバージョンはスキップされます。

## 関連ページ

- [はじめに](/wiki/getting-started/)
- [オーディオの設定](/wiki/audio-setup/)
- [トラブルシューティング](/wiki/troubleshooting/)
- [ダウンロード](/download/)
- [アップデート](/updates/)
