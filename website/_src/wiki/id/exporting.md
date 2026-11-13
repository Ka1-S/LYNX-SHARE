---
source: da5e41c322
---
![Jendela ekspor dengan WAV terpilih: rentang, tail, dan file di kiri, format di kanan, opsi mix di bawah](/assets/img/wiki/export.webp)

Semua yang keluar dari LYNX SHARE lewat `File › Ekspor`. Format audio (WAV, MP3, OGG, dan FLAC) memakai satu jendela ekspor. MIDI, proyek FL Studio, set Ableton Live, dan proyek zip punya entri sendiri di menu yang sama.

## Mengekspor lagu sebagai WAV

1. Pilih `File › Ekspor › File Wave...`.
2. Biarkan `Rentang` pada `Song (Playlist)` dan `File` pada `Satu file (mix)`.
3. Pilih `Bit depth`: 16 bit, 24 bit, atau 32 bit (float).
4. Atur `Tail` ke `Biarkan sisa` supaya reverb dan release berbunyi sampai habis setelah bar terakhir.
5. Klik `Ekspor...`, pilih nama dan folder, lalu tunggu render selesai.
6. Setelah selesai, LYNX SHARE melaporkan ke mana file disimpan dan level puncaknya.

`Ctrl+R` membuka jendela dengan format yang terakhir kamu ekspor. `Ctrl+Shift+R` membukanya pada MP3.

## Apa yang di-render

| Pengaturan | Fungsinya |
|---|---|
| `Rentang` | `Song (Playlist)`, `Pattern saat ini`, atau `Pilihan Playlist` |
| `Putar` | Berapa kali rentang diputar di dalam file, dari `Sekali` sampai 16 kali |
| `Tail` | `Potong sisa` berhenti tepat di akhir. `Biarkan sisa` membiarkan suara berbunyi sampai habis. `Lipat sisa` mencampur ekornya ke awal, untuk loop |
| `Tail terpanjang` | 3, 6, 12, atau 30 detik. Ekor berhenti lebih awal kalau suaranya sudah menghilang |
| `File` | Satu file, atau stem (lihat di bawah) |

## Format output

Empat tombol memilih formatnya. `Simpan juga` menulis format lain dari render yang sama, dengan nama yang sama.

| Format | Opsi |
|---|---|
| WAV | `Bit depth`: 16 bit, 24 bit, atau 32 bit (float). `Dither`: `Mati`, `TPDF`, atau `TPDF + noise shaping` |
| MP3 | `Bitrate`, konstan |
| OGG | Bitrate |
| FLAC | `Bit depth`, `Kompresi` dari `0 (tercepat)` sampai `8 (terkecil)`, `Dither`. Lossless di setiap level |

`Sample rate` dan `Channel` berlaku untuk semua format. `Channel` adalah `Stereo`, `Mono (kedua sisi dicampur)`, `Mono (kiri saja)`, atau `Mono (kanan saja)`.

## Opsi mix

| Pengaturan | Fungsinya |
|---|---|
| `Normalisasi (puncak ke 0 dB)` | Menaikkan atau menurunkan seluruh file sampai puncak tertingginya mencapai skala penuh |
| `Efek master` | Mati: efek insert Master di-bypass |
| `Efek insert` | Mati: efek insert lainnya di-bypass |
| `HQ untuk semua plugin` | Plugin memakai kualitas render. Mati: ekspor memakai kualitas pemutaran |
| `Pemanasan plugin` | Plugin pihak ketiga memproses sejenak keheningan sebelum ekspor dimulai |
| `Nonaktifkan polifoni maksimum` | Menghapus batas voice channel, jadi tidak ada not yang terpotong |
| `Pangkas senyap dari kompensasi delay` | Menghapus keheningan yang ditambahkan kompensasi latensi plugin di awal |
| `Informasi tempo`, `Penanda loop`, `Penanda playlist` | Khusus WAV: menyimpan tempo, loop, dan marker Playlist di dalam file |
| `Tampilkan file setelah selesai` | Membuka file manager pada hasilnya |

## Stem dan track

![Jendela ekspor diatur ke FLAC dengan satu file per insert mixer, total sembilan file](/assets/img/wiki/export-flac-stems.webp)

`File` punya tiga mode stem:

- `Satu per insert mixer (stem)`: satu file untuk setiap insert yang mengeluarkan suara.
- `Satu per channel (stem)`: satu file untuk setiap channel yang diputar.
- `Satu per channel, hanya instrumen (tanpa send atau bus)`.

Centang `Juga mix penuh` untuk mendapatkan mix di samping stem. Baris di bagian bawah menunjukkan berapa file yang akan ditulis.

`File › Ekspor › Semua track Playlist` dan `Semua grup track Playlist` menulis satu file per track atau per grup Playlist. Di menu Mixer, `Perekaman ke disk › Render track yang disiagakan ke file wave...` (`Alt+R`) membuka jendela ekspor hanya untuk insert yang disiagakan.

## Entri lain di menu Ekspor

| Perintah | Tombol | Fungsinya |
|---|---|---|
| `File MIDI...` | `Ctrl+Shift+M` | Menulis not sebagai file MIDI standar (`.mid`) |
| `Master...` | `Ctrl+Alt+M` | Me-render proyek dan me-master-nya ke target loudness dalam LUFS, di komputermu |
| `Proyek beserta sample (.zip)...` | tidak ada | Proyek beserta sample-nya dalam satu arsip |
| `Proyek FL Studio...` | tidak ada | Sebuah `.flp`, atau paket loop zip dengan semua sample |
| `Set Ableton Live (.als)...` | tidak ada | Set untuk Live 12 atau Live 11 |
| `Tulang proyek...` | tidak ada | Preset dan skor channel dan efek, dalam sebuah folder |

## Mengekspor ke FL Studio dan Ableton Live

`.flp` adalah proyek FL Studio 2026. Sampler, clip audio, generator dan efek bawaan FL Studio, plugin pihak ketiga, pattern, Playlist, Mixer, dan otomasi ikut terbawa beserta pengaturannya. Untuk hal-hal yang tidak dimiliki FL Studio, kamu memilih antara `Render ke audio (suaranya sama)` dan `Pakai plugin FL terdekat (tetap bisa diedit, suaranya bisa berbeda)`. Pada pilihan kedua, instrumen yang hanya ada di LYNX dilewati. Modulator selalu dilewati. Sebuah laporan mencantumkan apa yang di-render, diganti, atau dilewati. Rinciannya ada di [Pindahan dari FL Studio](/wiki/coming-from-fl-studio/).

`.als` berisi track MIDI per channel, track audio, grup, return, send, rack dan Drum Rack, plugin VST2 dan VST3 beserta state-nya, marker, serta otomasi volume, pan, dan tempo. Instrumen dan efek bawaan LYNX SHARE tidak dikonversi, tetapi track dan not-nya tetap disimpan. Otomasi parameter efek dan rute antar insert dibuang. Rinciannya ada di [Pindahan dari Ableton Live](/wiki/coming-from-ableton/).

## Baik diketahui

- `Dither` tampil abu-abu untuk MP3, dan opsi tempo serta marker tampil abu-abu untuk semua format selain WAV.
- Dengan `Pilihan Playlist` dan tanpa seleksi di Playlist, seluruh lagu di-render. Jendela memperingatkanmu.
- Kalau insert Master di-mute, jendela memperingatkan bahwa file-nya akan senyap.
- `Tulis juga video dengan mix ini` hanya muncul saat proyek punya track video, dan membutuhkan FFmpeg.
- `Render melalui hardware secara real-time` hanya muncul saat proyek punya insert hardware yang aktif. Ekspor itu memakan waktu sepanjang lagunya.
- Render to audio, saat sebuah insert di-bounce menjadi clip audio, diputar sekitar 5 dB lebih keras daripada insert yang digantikannya di versi ini.

## Terkait

- [Format file](/wiki/file-formats/)
- [Command line](/wiki/command-line/)
- [Pindahan dari FL Studio](/wiki/coming-from-fl-studio/)
- [Pindahan dari Ableton Live](/wiki/coming-from-ableton/)
- [Mixer](/wiki/mixer/)
