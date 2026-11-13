---
source: 358890815a
---
`File › Buka...` (`Ctrl+O`) membuka proyek `.lynx` milik LYNX SHARE sendiri dan juga file `.flp`, `.als`, `.flm`, dan `.mid`. Proyek dari program lain adalah impor: ia menjadi proyek LYNX SHARE baru yang belum disimpan, dan file aslinya tidak pernah ditulisi.

## Membuka proyek dari program lain

1. Pilih `File › Buka...` dan pilih file `.flp`, `.als`, `.flm`, atau `.mid`.
2. Tunggu selagi LYNX SHARE mencari plugin dan sample proyek itu.
3. Jika ada yang tidak ditemukan, jendela `File hilang` terbuka. Arahkan ke folder sample kamu, atau tutup.
4. Bilah judul sekarang menampilkan asal proyek, misalnya "(imported from FL Studio)".
5. Pilih `File › Simpan`. LYNX SHARE menanyakan tempat menulis file `.lynx`.

## Yang ikut terbawa

| Format | Dikonversi | Tidak dikonversi |
|---|---|---|
| `.flp` (FL Studio) | Channel, sample, pattern, not slide dan porta, semua aransemen, marker, perubahan birama, otomasi, tautan kontroler, Mixer beserta rutenya, sidechain, EQ track, dan efek bawaan | Paket FLEX dienkripsi dan tidak pernah dibuka. Channel FLEX mendapat patch synth yang dipilih berdasarkan nama presetnya |
| `.als` (Live 11 dan 12) | Track MIDI, audio, grup, dan return, tempo, birama, locator, loop, volume, pan, mute, solo, dan send beserta otomasinya, clip Session dan scene, warp marker, rack dan Drum Rack, dan banyak device bawaan Live (EQ Eight, Compressor, Glue Compressor, Reverb, Delay, Auto Filter, Saturator, Utility, Simpler, Drift, dan lainnya) sebagai milik LYNX SHARE sendiri | Device Live yang tidak punya padanan di LYNX SHARE (Hybrid Reverb, Grain Delay, Amp, Roar, patch FM Operator, dan lainnya) dibuang dan dicantumkan. MIDI Effect Rack tidak dikonversi |
| `.flm` (FL Studio Mobile) | Tempo, birama, satu insert mixer per channel, satu track Playlist per lajur, pattern, clip audio dengan offset dan pitch, drum kit, DW Sampler dan program `.dwp`, Slicer, MiniSynth, Transistor Bass, 3x Osc, GMSynth, dan SuperSaw, efek, otomasi, dan chord set | Generator FLM lainnya memakai synth bawaan |
| `.mid` | Not, birama, dan setiap perubahan tempo | File MIDI tidak membawa suara: channel mendapat synth LYNX SHARE atau channel MIDI Out |

Plugin pihak ketiga VST2, VST3, dan CLAP di file `.flp`, serta plugin VST2 dan VST3 di file `.als`, dimuat dengan state yang tersimpan jika plugin yang sama terinstal di komputermu.

## Plugin bawaan FL Studio

Generator dan efek FL Studio tidak dimuat. LYNX SHARE membaca pengaturan tersimpannya dan memainkannya di engine yang ia buat ulang. Setiap engine punya tingkat kualitas, dari `Exact` (identik bit demi bit dengan aslinya pada setiap state yang diuji) sampai `Placeholder` (pengaturan disimpan, tetapi plugin senyap atau hanya meneruskan audio). Tingkatannya dijelaskan di [Pindahan dari FL Studio](/wiki/coming-from-fl-studio/).

Membuka `.flp` bisa dilakukan tanpa FL Studio terinstal. Beberapa engine membutuhkan wave, sample, atau impuls yang dibaca dari instalasi FL Studio milikmu sendiri saat berjalan.

## Sample yang hilang

![Jendela Missing files pada tab Samples: file yang ditemukan berwarna hijau, yang hilang berwarna kuning tua, dengan Search folder, Locate, dan Skip](/assets/img/wiki/missing-files.webp)

Tab `Sample` mencantumkan setiap sample yang tidak ditemukan, beserta path tersimpannya dan apa yang memakainya.

| Perintah | Fungsinya |
|---|---|
| `Cari di folder...` | Mencari semua sample yang hilang di sebuah folder beserta subfoldernya |
| `Cari lokasi...` | Menanyakan lokasi satu sample. Sample lain di folder yang sama ikut terambil |
| `Lewati` | Membiarkan sample itu tidak dipakai. Channel-nya tetap senyap |

`Terapkan` memakai file yang ditemukan.

Tab `Ditemukan otomatis` mencantumkan file yang ditemukan LYNX SHARE sendiri di folder lain. Simpan proyek untuk mempertahankan lokasi barunya.

## Plugin yang hilang

LYNX SHARE mencari setiap plugin pihak ketiga lewat pengenalnya, lalu lewat filenya, lalu lewat namanya. Proyek yang disimpan dengan VST2 terbuka dengan VST3 dari produk yang sama, dan sebaliknya, tetapi dengan pengaturan default.

Saat sebuah efek tidak terinstal, efek LYNX SHARE dengan peran yang sama mengambil alih slotnya: reverb untuk reverb, compressor untuk compressor.

- Slot menampilkan nama plugin aslinya dengan lencana LYNX SHARE.
- Jika state yang tersimpan bisa dibaca, pengaturannya diterjemahkan. Jika tidak, pengganti dimulai dari preset default. Tabel produk, penggantinya, dan apa yang diterjemahkan untuk masing-masing ada di [PLUGIN_FALLBACK.md](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/PLUGIN_FALLBACK.md).
- Otomasi parameter plugin asli tetap disimpan tetapi tidak menggerakkan pengganti.
- Referensi dan state plugin asli tetap ada di proyek. Setelah plugin itu terinstal, proyek terbuka lagi dengannya.
- `Bypass semua pengganti LYNX SHARE di proyek ini`, di menu klik kanan slot pengganti, mematikan semuanya sekaligus.

Sakelarnya adalah `Ganti plugin yang hilang dengan padanan LYNX SHARE` di `Opsi › Pengaturan umum...`, aktif secara default. Jika dimatikan, slot efek yang hilang dibiarkan keluar dari mixer. Instrumen yang hilang tidak diganti. Ia menjadi synth bawaan dengan preset yang dipilih berdasarkan nama channel.

Untuk mendapatkan plugin aslinya kembali, instal, pilih `Opsi › Pindai plugin`, lalu buka proyek lagi.

## File MIDI

![Jendela Import MIDI data: track, channel, jenis channel, dan opsi](/assets/img/wiki/midi-import-dialog.webp)

`File › Buka...` membuat proyek baru dari file MIDI. `File › Impor › File MIDI...` lebih dulu membuka jendela `Impor data MIDI`, tempat kamu memilih apa yang dibawa masuk.

| Pengaturan | Fungsinya |
|---|---|
| `Track mana yang diimpor?` | Semua track, atau satu |
| `Channel mana yang diimpor?` | Klik channel MIDI untuk mengecualikannya. Klik kanan hanya mempertahankan yang itu |
| `Jenis channel` | `Synth LYNX SHARE` atau `MIDI Out` |
| `Mulai proyek baru` | Memulai proyek kosong. Nonaktif: menambahkan ke proyek yang terbuka |
| `Buat satu channel per track` | Nonaktif: satu channel per channel MIDI |
| `Atur track mixer untuk channel baru` | Merutekan channel baru ke insert mixer berurutan |
| `Impor birama` | Birama file menjadi birama proyek |

`Ctrl+M` di [Piano Roll](/wiki/piano-roll/) mengimpor file MIDI ke channel saat ini.

## Perlu diketahui

- `Bantuan › File hilang...` membuka jendela itu lagi nanti. `Bantuan › Last FL Studio import report` mencantumkan apa yang didekati, tidak ikut, atau tidak terinstal.
- Clip Session dari set Live masuk ke kisi klip pada layout Clip-launch. `Klip sesi dari set Live` di `Opsi › Pengaturan umum...` menaruhnya di Playlist sebagai gantinya.

## Terkait

- [Format file](/wiki/file-formats/)
- [Pindahan dari FL Studio](/wiki/coming-from-fl-studio/)
- [Pindahan dari Ableton Live](/wiki/coming-from-ableton/)
- [Plugin](/wiki/plugins/)
- [Ekspor](/wiki/exporting/)
