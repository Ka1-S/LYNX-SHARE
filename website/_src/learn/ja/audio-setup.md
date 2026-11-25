---
source: 6d1d7a664e
---
## ドライバーと出力

![設定のAudioページ](/assets/img/learn/audio-setup-1.webp)

1. `オプション › オーディオ設定...` を開きます。
2. ドライバーを選びます: WindowsはASIO（お使いのインターフェース専用のもの、またはFL Studio ASIO）、MacはCore Audio、LinuxはPipeWireかALSAです。
3. `出力デバイス` はLYNXの音を聴く場所です。`System default` は、パソコンが使っている出力に合わせます。

## バッファーサイズ

![バッファーサイズとレイテンシー](/assets/img/learn/audio-setup-2.webp)

1. `バッファーサイズ`: バッファーが小さいほど、キーボードを弾いたときの反応が速くなります。大きいほど、CPUに余裕ができます。
2. プツプツ鳴る、または音が途切れる？ 上げてください。弾いた音が遅れる？ 下げてください。

## 音を確認する

![Help、Test audio output](/assets/img/learn/audio-setup-3.webp)

1. `テストサウンドを再生` は、選んだ出力から短い音を鳴らします。
2. どこからでも、`ヘルプ › オーディオ出力のテスト...` で同じことができます。

[オーディオ設定](/wiki/audio-setup/)と[トラブルシューティング](/wiki/troubleshooting/)もご覧ください。
