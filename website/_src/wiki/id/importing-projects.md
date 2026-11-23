---
source: 532b2eee04
---
`File › Buka...` (`Ctrl+O`) membuka proyek `.lynx` milik LYNX SHARE sendiri dan juga file `.flp`, `.als`, `.flm`, `.dawproject`, `.band`, `.blx`, dan `.mid`. Proyek dari program lain adalah impor: ia menjadi proyek LYNX SHARE baru yang belum disimpan, dan file aslinya tidak pernah ditulisi.

## Membuka proyek dari program lain

1. Pilih `File › Buka...` dan pilih file `.flp`, `.als`, `.flm`, `.dawproject`, `.band`, `.blx`, `.mid`, `.zip`, atau modul tracker (`.mod`, `.xm`, `.s3m`, `.it`).
2. Tunggu selagi LYNX SHARE mencari plugin dan sample proyek itu.
3. Jika ada yang tidak ditemukan, jendela `File hilang` terbuka. Arahkan ke folder sample kamu, atau tutup.
4. Bilah judul sekarang menampilkan asal proyek, misalnya "(imported from FL Studio)".
5. Pilih `File › Simpan`. LYNX SHARE menanyakan tempat menulis file `.lynx`.

## Yang ikut terbawa

| Format | Dikonversi | Tidak dikonversi |
|---|---|---|
| `.flp` (FL Studio) | Channel, sample, pattern, not slide dan porta, semua aransemen, marker, perubahan birama, otomasi, tautan kontroler, Mixer beserta rutenya, sidechain, EQ track, dan efek bawaan | Paket FLEX dienkripsi dan tidak pernah dibuka. Channel FLEX mendapat patch synth yang dipilih berdasarkan nama presetnya |
| `.als` (Live 11 dan 12) | Track MIDI, audio, grup, dan return, tempo, birama, locator, loop, volume, pan, mute, solo, dan send beserta otomasinya, clip Session dan scene, warp marker, rack dan Drum Rack, dan banyak device bawaan Live (EQ Eight, Compressor, Glue Compressor, Reverb, Hybrid Reverb, Delay, Grain Delay, Auto Filter, Saturator, Roar, Utility, Simpler, Drift, Operator, Wavetable, dan lainnya) sebagai milik LYNX SHARE sendiri | Device Live yang tidak punya padanan di LYNX SHARE (Amp, Cabinet, Beat Repeat, Vocoder, dan lainnya) dibuang dan dicantumkan |
| `.flm` (FL Studio Mobile) | Tempo, birama, satu insert mixer per channel, satu track Playlist per lajur, pattern, clip audio dengan offset dan pitch, drum kit, DW Sampler dan program `.dwp`, Slicer, MiniSynth, Transistor Bass, 3x Osc, GMSynth, dan SuperSaw, efek, otomasi, dan chord set | Generator FLM lainnya memakai synth bawaan |
| `.dawproject` (Bitwig Studio, Studio One, Cubase, dan aplikasi lain) | Track, folder, clip, not, audio beserta penanda warp-nya, otomasi, tempo, marker, mixer dengan send, scene ke dalam grid clip, dan plugin VST3, VST2, dan CLAP beserta state-nya. EQ, compressor, gate, dan limiter dari format ini menjadi milik LYNX SHARE sendiri | Efek not, ekspresi per not, dan device bawaan aplikasi lain dicantumkan di laporan impor |
| `.band` (GarageBand 10, GarageBand untuk iOS) | Track, region MIDI dan audio, tempo, birama, nada dasar, section sebagai marker, level, pan, dan mute, sebagai track Alur kerja sederhana. Instrumen Sampler yang dibuat di GarageBand untuk iOS | Track instrumen memainkan synth dan drum bawaan. Region Drummer, folder take, dan perubahan tempo dicantumkan. Proyek dari GarageBand 11 dan yang lebih lama tidak bisa dibaca |
| `.blx` (BandLab) | Track, region MIDI dan audio beserta loop-nya, tempo, birama, nada dasar, volume, pan, dan mute, otomasi volume dan pan, dan efek umum, sebagai track Alur kerja sederhana | Track instrumen memainkan synth dan drum bawaan. Efek lain dan preset Mastering dilewati dan dicantumkan. Hanya diuji pada proyek rekaan |
| `.mid` | Not, birama, dan setiap perubahan tempo | File MIDI tidak membawa suara: channel mendapat synth LYNX SHARE atau channel MIDI Out |
| `.mod`, `.xm`, `.s3m`, `.it` (modul tracker) | Instrumen sebagai channel Sampler beserta sample-nya, pattern, daftar urutan sebagai Playlist, kecepatan dan tempo, volume not, pan, delay, dan cut; perubahan volume dan pan sebagai event channel (MOD dan XM), serta slide, vibrato, arpeggio, dan envelope pitch sebagai kurva pitch not | Efek lain dan envelope instrumen dihitung di laporan impor. `Tampilan › Tracker` mengedit not dengan cara tracker |
| `.zip` | Paket loop zip FL Studio, atau proyek yang diekspor beserta sample-nya: diekstrak ke folder tersendiri, lalu proyek di dalamnya dibuka beserta sample-nya | |

Plugin pihak ketiga VST2, VST3, dan CLAP di file `.flp` atau `.dawproject`, serta plugin VST2 dan VST3 di file `.als`, dimuat dengan state yang tersimpan jika plugin yang sama terinstal di komputermu.

## Plugin bawaan FL Studio

Generator dan efek FL Studio tidak dimuat. LYNX SHARE membaca pengaturan tersimpannya dan memainkannya di engine yang ia buat ulang. Setiap engine punya tingkat kualitas, dari `Exact` (identik bit demi bit dengan aslinya pada setiap state yang diuji) sampai `Placeholder` (pengaturan disimpan, tetapi plugin senyap atau hanya meneruskan audio). Tingkatannya dijelaskan di [Pindahan dari FL Studio](/wiki/coming-from-fl-studio/).

Membuka `.flp` bisa dilakukan tanpa FL Studio terinstal. Beberapa engine membutuhkan wave, sample, atau impuls yang dibaca dari instalasi FL Studio milikmu sendiri saat berjalan.

File `.flp` yang memakai "FL Studio Mobile Rack" milik FL Studio ("Save as FLP" milik FL Studio Mobile menulis satu per track instrumen) memainkan instrumen dan efeknya dengan engine yang sama seperti `.flm`, begitu juga efek "FL Studio Mobile Rack FX". Rack dengan drum kit atau beberapa instrumen masih dimainkan dengan synth bawaan.

## Suara bawaan pabrik FL Studio Mobile

Suara bawaan pabrik yang dipakai file `.flm` (drum kit, program DirectWave, SoundFont, wave GMSynth) milik FL Studio Mobile dan tidak pernah disertakan bersama LYNX SHARE. LYNX SHARE mencarinya di folder `Resources` salinan aplikasi milikmu sendiri: tambahkan di `Pengaturan › File › Sumber daya FL Studio Mobile` (memilih folder di atasnya, atau folder instalasi FL Studio, juga bisa). Instalasi FL Studio hanya memuat sebagian darinya, dan dicari setelah folder milikmu. File yang masih hilang dicantumkan di jendela File hilang.

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

## Impor track dari proyek lain

`File › Impor › Trek dari proyek lain...` membawa track dari proyek lain ke proyek yang terbuka, tanpa menutupnya. Ini membaca semua format yang dibaca `File › Buka...`.

1. Pilih proyeknya. Sebuah jendela mencantumkan track Playlist-nya dan isi masing-masing.
2. Centang track yang kamu inginkan.
3. Pilih apakah `Dengan insert mixer dan efek` dan `Dengan otomasi` ikut dibawa, dan `Tempatkan di` awal lagu atau awal seleksi waktu.
4. Klik untuk mengimpor. Track, channel, pattern, dan insert baru datang sebagai satu langkah, jadi satu kali undo mengembalikan semuanya.

## Perlu diketahui

- `Bantuan › File hilang...` membuka jendela itu lagi nanti. `Bantuan › Laporan impor proyek terakhir` mencantumkan apa yang didekati, tidak ikut, atau tidak terinstal.
- Clip Session dari set Live masuk ke kisi klip pada layout Clip-launch. `Klip sesi dari set Live` di `Opsi › Pengaturan umum...` menaruhnya di Playlist sebagai gantinya.

## Terkait

- [Format file](/wiki/file-formats/)
- [Pindahan dari FL Studio](/wiki/coming-from-fl-studio/)
- [Pindahan dari Ableton Live](/wiki/coming-from-ableton/)
- [Plugin](/wiki/plugins/)
- [Ekspor](/wiki/exporting/)
