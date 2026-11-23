---
source: e1ff018baf
---
LYNX SHARE menyimpan proyek sebagai file `.lynx`. Program ini juga membuka proyek dari program lain, memuat file audio yang umum, dan mengekspor audio, MIDI, dan proyek untuk program lain. Tabel mencantumkan setiap format di 0.1.31 alpha.

Proyek yang dibuka dari format program lain adalah impor. `File › Simpan` lalu menanyakan tempat menulis file `.lynx`, dan file aslinya tidak pernah diubah.

## Membuka proyek dari program lain

1. Pilih `File › Buka...` (`Ctrl+O`).
2. Pilih file `.flp`, `.als`, `.flm`, `.dawproject`, `.band`, `.blx`, `.mid`, atau `.zip`.
3. Pilih `Bantuan › Laporan impor proyek terakhir` untuk membaca apa yang diperkirakan atau dilewati.
4. Pilih `File › Simpan` lalu tentukan tempat menulis file `.lynx`.

## Format

| Format | Buka | Simpan / Ekspor | Catatan |
|---|---|---|---|
| `.lynx` | `File › Buka...` | `File › Simpan`, dengan simpan otomatis | Format proyek milik LYNX SHARE (JSON). `File › Simpan salinan untuk LYNX yang lebih lama...` menulis salinan yang bisa dibuka 0.1.27 atau 0.1.28, dan mencantumkan apa yang tidak bisa disimpan versi itu |
| `.zip` | `File › Buka...` mengekstraknya ke folder dan membuka proyek di dalamnya | `File › Ekspor › Paket loop zip...` | Juga membuka paket loop zip FL Studio |
| `.lynxpack` | Terbuka setelah ringkasan. Sebuah bagian digabungkan ke proyek host | `File › Sesi online › Kirim proyek...` | Proyek atau bagian yang dikirim dalam [sesi online](/wiki/online-sessions/). Bagian plugin pihak ketiga dikirim sebagai render audio plus pengaturan plugin |
| `.flp` | `File › Buka...` | `File › Ekspor › Proyek FL Studio...`, proyek FL Studio 2026 atau paket loop zip | Plugin bawaan dimainkan di engine buatan ulang dengan tingkat kemiripan yang beragam. Paket FLEX tidak pernah dibuka. Saat ekspor, instrumen dan efek yang hanya ada di LYNX di-render ke audio atau ditukar dengan plugin FL terdekat, dan modulator dilewati. Lihat [Pindahan dari FL Studio](/wiki/coming-from-fl-studio/) |
| `.flm` | `File › Buka...` | `File › Simpan salinan sebagai proyek FL Studio Mobile (.flm)...` | Proyek FL Studio Mobile. Parsial: instrumen selain MiniSynth, Transistor Bass, 3x Osc, GMSynth, SuperSaw, DW Sampler, Slicer, dan DirectWave dimainkan dengan synth bawaan. Ekspor ini berupa salinan: not, tempo, mixer, instrumen milik FL Studio Mobile, DW Sampler, 3x Osc, drum kit, efek, dan otomasi level mixer, panning, efek, serta DW Sampler dan GMSynth ikut terbawa, dengan sample di sebelahnya. Dalam salinan, instrumen lain mempertahankan notnya di MiniSynth, sedangkan efek tanpa modul FLM dan otomasi target lain (kenop milik synth, plugin pihak ketiga) dibuang; sebuah jendela mencantumkan semuanya sebelum menyimpan. |
| `.als` | `File › Buka...`, set Live 11 dan 12 | `File › Ekspor › Set Ableton Live (.als)...`, untuk Live 12 atau Live 11 | Saat impor, device Live yang tidak punya padanan di LYNX SHARE dibuang. Saat ekspor, instrumen dan efek bawaan LYNX SHARE tidak dikonversi (Helix menjadi Wavetable), dan otomasi parameter efek serta rute antar insert dibuang. Lihat [Pindahan dari Ableton Live](/wiki/coming-from-ableton/) |
| `.dawproject` | `File › Buka...` | `File › Ekspor › DAWproject (.dawproject)...` | Format pertukaran terbuka milik Bitwig, Studio One, Cubase, dan aplikasi lain: track, folder, clip, not, audio beserta penanda warp-nya, otomasi, tempo, penanda, mixer dengan send, dan plugin VST3, VST2, dan CLAP beserta statusnya. Efek not dan ekspresi MIDI dilewati saat impor; grid clip, aransemen lain, dan beberapa detail per not dilewati saat ekspor. Kedua arah mencantumkan apa yang dilewati |
| `.band` | `File › Buka...` (paketnya, file `projectData`-nya, atau zip-nya) | Tidak | Proyek GarageBand 10 atau GarageBand untuk iOS, sebagai track Alur kerja sederhana. Parsial: track instrumen memainkan synth dan drum bawaan, kecuali instrumen Sampler yang dibuat di GarageBand untuk iOS. Proyek dari GarageBand 11 dan yang lebih lama tidak bisa dibaca |
| `.blx` | `File › Buka...`, atau foldernya dengan impor `Folder...` Alur kerja sederhana | Tidak | Proyek BandLab yang diunduh oleh Cakewalk atau Sonar, sebagai track Alur kerja sederhana dengan region, tempo, nada dasar, mix, otomasi volume dan pan, dan efek umum. Parsial: suara instrumen BandLab dan pedal lainnya dicantumkan di laporan impor |
| `.mid`, `.midi` | `File › Buka...` membuat proyek baru. `File › Impor › File MIDI...` dan Piano Roll mengimpor ke proyek yang sedang terbuka | `File › Ekspor › File MIDI...` (`Ctrl+Shift+M`) | |
| `.mod`, `.xm`, `.s3m`, `.it` | `File › Buka...` | Tidak | Modul tracker (ProTracker, FastTracker 2, Scream Tracker 3, Impulse Tracker). Instrumennya menjadi channel Sampler dengan sample-nya, lagunya menjadi klip pattern dengan perubahan temponya, dan `Tampilan › Tracker` mengedit not-notnya. Sebagian: efek seperti slide, vibrato dan arpeggio, envelope, serta perubahan volume pada not yang sedang berbunyi dicantumkan di laporan impor |
| `.wav` | Sebagai sample atau clip audio | `File › Ekspor › File Wave...`, 16 bit, 24 bit, atau 32 bit float | Dither, informasi tempo, marker loop, dan marker Playlist bersifat opsional |
| `.mp3` | Sebagai sample atau clip audio | `File › Ekspor › File MP3...` | Bitrate konstan |
| `.ogg` | Sebagai sample atau clip audio | `File › Ekspor › File OGG...` | |
| `.flac` | Sebagai sample atau clip audio | `File › Ekspor › File FLAC...`, kompresi 0 sampai 8 | Lossless di setiap level |
| `.aif`, `.aiff` | Sebagai sample atau clip audio | `File › Ekspor › File AIFF...`, 16 bit atau 24 bit | |
| `.m4a` | – | `File › Ekspor › File M4A...` | Hanya ekspor. AAC dengan encoder sistem di Windows dan macOS; tidak tersedia di Linux |
| `.wv` | Sebagai sample | Tidak | Sample WavPack, seperti yang disertakan di paket FL Studio |
| `.sf2`, `.sf3` | Dimainkan oleh LYNX Soundfont | Tidak | SoundFont |
| `.dwp`, `.dwb` | Klik ganda di Browser, drop di Channel Rack, atau `Buka program / bank...` milik DirectWave | Simpan program dan bank DirectWave | Program dan bank DirectWave. Sample dicari di tempat DirectWave mencarinya |
| `.gig` | Sebagai channel DirectWave | Tidak | Instrumen GigaStudio: layer velocity, round robin, loop, tuning, dan envelope amp. Sample release-trigger, keyswitch, dan sample terkompresi belum dibaca |
| `.ncw` | Sebagai sample atau clip audio | Tidak | Sample terkompresi Kontakt. Instrumen Kontakt `.nki` memerlukan Kontakt itu sendiri sebagai plugin; library yang dilindungi tidak pernah didekripsi |
| `.fst` | Dari Browser | Tidak | Preset plugin bawaan FL Studio. Hanya untuk plugin yang punya engine di LYNX SHARE |
| `.fsc` | Piano Roll dan Editor event | Piano Roll dan Editor event | File skor dan otomasi FL Studio |
| `.lynxpreset`, `.fxp`, `.fxb`, `.vstpreset` | `Muat file preset...` di menu preset plugin yang di-host | `Simpan preset...` dan `Ekspor file preset...` | Preset plugin pihak ketiga. `.fxp` dan `.fxb` untuk plugin VST2 dan `.vstpreset` untuk plugin VST3, dan hanya selama plugin berjalan di dalam LYNX SHARE |
| File video | `File › Impor › File video...` menaruh video di Playlist | `Tulis juga video dengan mix ini` di jendela ekspor | Impor memakai decoder sistem di Windows dan macOS, dan membutuhkan FFmpeg di Linux. Ekspor membutuhkan FFmpeg |

## Ekspor lainnya

| Perintah | Fungsinya |
|---|---|
| Opsi `File` di jendela ekspor | Stem: satu file per insert mixer, per channel, atau per channel hanya dengan instrumen |
| `File › Ekspor › Semua track Playlist` | Satu file per track Playlist |
| `File › Ekspor › Semua grup track Playlist` | Satu file per grup track |
| `File › Ekspor › Master...` | Me-render proyek dan me-master-nya ke target loudness, di komputermu |
| `File › Ekspor › Untuk aplikasi lain...` | Satu folder berisi stem, file MIDI, set Ableton Live, proyek FL Studio, mixdown, dan README, di-zip kalau kamu mau |
| `File › Ekspor › Tulang proyek...` | Menulis preset dan skor channel dan efek ke sebuah folder |

Jendela ekspor me-render lagu, pattern saat ini, atau seleksi Playlist.

## Dari command line

Di Windows programnya adalah `LYNX.exe`. Di macOS dan Linux namanya `LYNX`.

```
LYNX.exe project.lynx                       open a project (any file File › Open takes)
LYNX.exe --export project.flp out.flac      render the song (wav, flac, ogg, mp3, aif; m4a on Windows and macOS)
LYNX.exe --stems project.flp base.wav       one WAV per mixer insert that has sound
LYNX.exe --export-flp project.lynx out.flp  write an FL Studio project
```

`--export`, `--stems` dan `--export-flp` juga menerima `.als` sebagai input.

## Baik diketahui

- File audio yang lebih panjang dari 30 detik di-stream dari cache disk.
- `.flp`, `.als`, `.flm`, `.dawproject`, `.band`, atau `.blx` tidak pernah ditulis saat kamu menyimpan. Perintah ekspor menulis file baru.
- File `.wv` hanya bisa dibaca.
- `.zip` diekstrak ke folder dulu, lalu proyek di dalamnya dibuka.

## Terkait

- [Mengimpor proyek](/wiki/importing-projects/)
- [Ekspor](/wiki/exporting/)
- [Command line](/wiki/command-line/)
- [Pindahan dari FL Studio](/wiki/coming-from-fl-studio/)
- [Pindahan dari Ableton Live](/wiki/coming-from-ableton/)
