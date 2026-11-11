---
source: 98829adcb8
---
LYNX SHAREは、ウィンドウを開かずに実行することもできます。プロジェクトをオーディオにレンダリングするオプション、FL StudioプロジェクトやAbleton Liveセットに変換するオプション、テスト用のオプションがいくつかあります。プロジェクト自身のチェックも、同じオプションを使っています。

プログラムは、Windowsでは `LYNX.exe`、macOSとLinuxでは `LYNX` です。macOSでのフルパスは `/Applications/LYNX.app/Contents/MacOS/LYNX` です。オプションはどのシステムでも同じです。

## プロジェクトをファイルにレンダリングする

1. プログラムのあるフォルダーでターミナルを開きます。Windowsでは、インストール先のフォルダーか、zipを展開したフォルダーです。
2. `LYNX.exe --export project.flp out.flac` を実行します。プロジェクトと出力ファイルは、ご自身のものに置き換えてください。
3. プログラムが終了するまで待ちます。ウィンドウは開きません。
4. 出力ファイルの隣に書き出された `out.flac.txt` を読みます。読み込んだプロジェクトの場合、変換されたもの、近似されたもの、省かれたもの、そして見つかったプラグインと見つからなかったプラグインが一覧になっています。

## プロジェクトを開く

```
LYNX.exe project.lynx
```

ウィンドウつきでLYNX SHAREを起動し、そのファイルを開きます。受け付けるのは `.lynx`、`.flp`、`.als`、`.flm`、`.mid` です。相対パスは、現在のフォルダーを基準にします。

## 普段使いのオプション

| オプション | 働き |
|---|---|
| `--export <project> <out>` | エクスポートウィンドウと同じように、テールを含む曲全体をレンダリングする。`<out>` の拡張子で形式が決まる: `.wav` (24ビット)、`.flac`、`.ogg`、`.mp3` (320 kbps) |
| `--export ... --pattern` | 現在のパターンだけをレンダリングする |
| `--export ... --insert N` | ミキサーのインサートNの出力だけをレンダリングする |
| `--export ... --rate N` | 別のサンプルレート (Hz) でレンダリングする |
| `--stems <project> <base.wav>` | チャンネルのあるミキサーのインサートごとに1つのWAVを書き出す: `base - 1.wav`、`base - 2.wav` のように続く |
| `--export-flp <project> <out.flp>` | FL Studioプロジェクトを書き出す。`out.zip` を指定すると、zip形式のループパッケージを書き出す |
| `--export-flp ... --substitute` | LYNX専用のプラグインを、オーディオにレンダリングする代わりに、最も近いFLのプラグインに置き換える |
| `--export-als <project> <folder>` | Live 12向けに、Ableton Liveセットを `<folder>/<name> Project/<name>.als` に書き出す |
| `--export-als ... --live11` | Live 11向けのセットを書き出す |
| `--devices <report.txt>` | このコンピューターのオーディオドライバーの種類とデバイスを一覧にする |
| `--showcase <out.lynx>` | LYNX SHARE独自のデモプロジェクトを書き出す |
| `--lang <id>` | 今回の実行だけ、別のインターフェース言語を使う。例: `--lang pt-BR` |
| `--menu-entry on` または `オフ` | Linux AppImageのみ: LYNX SHAREをアプリケーションメニューに追加する、または削除する |

`--export`、`--stems`、`--export-flp` は、プロジェクトとして `.lynx`、`.flp`、`.als` を受け付けます。`--export-als` は `.flm` も受け付けます。

例:

```
LYNX.exe --export song.lynx song.wav
LYNX.exe --export song.lynx drums.wav --insert 1
LYNX.exe --stems song.lynx song.wav
LYNX.exe --export-flp song.lynx song.flp
LYNX.exe --export-als song.lynx out-folder --live11
LYNX.exe --devices devices.txt
LYNX --lang de
```

## テスト用のオプション

LYNX SHARE自体を検査するためのオプションです。バグ報告に役立ちます。

| オプション | 働き |
|---|---|
| `--selftest <report.txt> [demo.lynx]` | オーディオデバイスなしでセルフテストを実行する。合格すると、レポートの末尾が `result=OK` になる |
| `--snapshot <prefix> [project]` | インターフェースとそのウィンドウを描画し、プレフィックスを付けたPNGファイルに書き出す。このWikiの画像はこの方法で作っている |
| `--render <project> <out.wav> <seconds>` | 現在のパターンを指定した秒数だけ、24ビットWAVにレンダリングする |
| `--plugintest <plugin> <report.txt>` | VST3またはCLAPのプラグインを1つ読み込んでテストする |
| `--sandboxtest <plugin> <report.txt>` | フリーズしたプラグインを含め、別プロセスでプラグインをテストする |
| `--scantest <folder> <report.txt>` | フォルダー内のプラグインを、別プロセスでスキャンする |
| `--fldb <report.txt> [project.flp]` | ここで見つかったFL Studioのインストール、そのプラグインデータベース、プロジェクトのプラグインの対応を報告する |
| `--devicetest <type> <device> <report.txt>` | オーディオドライバーを開いて1秒間の無音を流し、結果を報告する |
| `--uiperf <project> <report.txt>` | ミキサー、プレイリスト、チャンネルラックの描画時間を計測する |

例:

```
LYNX.exe --selftest report.txt
LYNX.exe --snapshot shots/s project.lynx
LYNX --devicetest PipeWire "System default" out.txt
```

## 知っておくと便利なこと

- 結果はターミナルではなく、ファイルに出力されます。`--export` は `<out>.txt`、`--export-flp` は `<out.flp>.txt`、`--export-als` はフォルダー内の `export-als.txt`、`--stems` は `<base.wav>.txt` に書きます。
- プログラムは標準入力を読みます。シェルのループの中では、各呼び出しに `< /dev/null` を付けてください。
- `--snapshot` は、環境変数 `LYNX_SNAPSHOT_SIZE` で指定しない限り、1280x720で描画します。指定の例: `1920x1080`。
- `--lang` には、`オプション › 全般設定...` に並ぶ言語のIDを指定できます。`en`、`pt-BR`、`de`、`es`、`fr`、`ja` などです。未知のIDは無視されます。
- `--export` はエクスポートウィンドウと同じレンダリング経路を使いますが、ウィンドウのほかのオプションは使いません。ディザー、ビット深度、ノーマライズが必要なときは、ウィンドウを使ってください。
- ソースには、これ以外にも多くのテスト用オプションがあります。内部用で、バージョンによって変わることがあります。

## 関連ページ

- [エクスポート](/wiki/exporting/)
- [ファイル形式](/wiki/file-formats/)
- [ソースからのビルド](/wiki/build-from-source/)
- [トラブルシューティング](/wiki/troubleshooting/)
