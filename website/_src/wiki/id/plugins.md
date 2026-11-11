---
source: 45c1275215
---
![Bagian bawah Pengelola plugin, dengan catatan bahwa setiap file plugin diperiksa di proses terpisah](/assets/img/wiki/plugin-manager-scan.webp)

LYNX SHARE menjalankan plugin instrumen dan efek dari pembuat lain. Ia menemukannya dengan memindai folder plugin, dan `Opsi › Pengelola plugin...` menampilkan apa yang ditemukan. Instrumen dan efek milik LYNX SHARE sendiri dijelaskan di [Instrumen dan efek bawaan](/wiki/built-in/).

## Memindai dan menambahkan plugin

1. Pilih `Opsi › Pindai plugin`. Progresnya tampil di bilah petunjuk dan kamu tetap bisa bekerja.
2. Jika plugin ada di folder tersendiri, buka `Opsi › Pengelola plugin...`, klik `Folder plugin...`, tambahkan folder itu, lalu klik `Cari plugin baru`.
3. Tekan `F8` untuk pemilih plugin dan ketik sebagian nama plugin.
4. Klik dua kali atau tekan `Enter`. Instrumen menjadi channel baru. Efek masuk ke insert Mixer yang dipilih.

Pada awal pertama, wizard pengaturan mencantumkan folder plugin dan memindainya di latar belakang. `Opsi › Wizard penyiapan (bahasa, audio, folder plugin)...` membukanya lagi. LYNX SHARE juga mencari plugin baru beberapa detik setelah setiap awal.

## Format per sistem

| Sistem | Format yang dijalankan |
|---|---|
| Windows | VST3, CLAP, VST2 |
| macOS | VST3, CLAP, Audio Units |
| Linux | VST3, CLAP, LV2, VST2 |

VST2 tidak dijalankan di macOS. Di Linux, plugin Windows bekerja lewat [yabridge](https://github.com/robbert-vdh/yabridge) dan Wine, yang kamu instal sendiri: tambahkan folder tujuan sinkronisasi yabridge sebagai folder plugin.

## Pengelola plugin

| Kontrol | Fungsinya |
|---|---|
| Kotak pencarian | Menyaring berdasarkan nama, vendor, format, atau file |
| `Semua`, `Instrumen`, `Efek`, `Favorit`, `Gagal` | Memilih plugin mana yang dicantumkan |
| Kolom bintang | Menandai favorit |
| Kolom `Bit` | 32, 64, atau 32+64 (Windows) |
| Kolom `Status` | `OK`, `File tidak ditemukan`, atau `Gagal dimuat` |
| `Cari plugin baru` | Memindai file yang belum ada di daftar |
| `Pindai ulang semua...` | Melupakan file yang gagal dan memeriksa ulang setiap file |
| `Folder plugin...` | Mengedit folder yang dipindai |
| `Tambah file plugin...` | Menambahkan satu file plugin |

Setiap file plugin diperiksa di proses terpisah. File yang crash atau hang selama 30 detik ditandai gagal dan dilewati kemudian. Di Windows, file yang gagal dicantumkan beserta alasannya di `%APPDATA%\LYNX\scan-failures.log`.

Folder standar dipindai, ditambah yang kamu tambahkan:

- Windows: `Common Files\VST3`, `Common Files\CLAP`, `Common Files\VST2`, `VSTPlugins` dan `Steinberg\VSTPlugins` di bawah Program Files, serta folder yang dipakai FL Studio jika terinstal.
- macOS: `VST3`, `Components`, dan `CLAP` di `~/Library/Audio/Plug-Ins` dan `/Library/Audio/Plug-Ins`.
- Linux: `~/.vst3`, `~/.clap`, `~/.lv2`, `~/.vst`, dan folder yang sama di bawah `/usr/lib` dan `/usr/local/lib`.

## Menjalankan plugin di proses terpisah

Secara default plugin berjalan di dalam LYNX SHARE, sehingga plugin yang crash bisa menjatuhkan programnya. Sebagai opsi, setiap plugin berjalan di prosesnya sendiri.

1. Buka `Opsi › Pengaturan umum...`.
2. Nyalakan `Jalankan plugin di proses terpisah (melindungi LYNX SHARE dari crash)`.
3. Buka ulang proyek agar berlaku pada plugin yang sudah terbuka.

Jika plugin semacam itu crash atau berhenti merespons, ia dibisukan, pesan muncul, dan LYNX SHARE tetap berjalan. Simpan proyek dan buka ulang untuk memuat plugin itu lagi. Editor plugin terbuka di jendelanya sendiri.

Untuk melakukannya pada satu plugin saja, pakai `Jalankan di proses terpisah` atau `Always run <plugin> in a separate process` di menu wrapper jendela plugin. Saat editor plugin crash, pesan yang muncul juga menawarkan yang kedua.

Ini bekerja di Windows dan Linux, dan sejak 0.1.25 di macOS.

## Plugin 32-bit (khusus Windows)

Di Windows, plugin VST2 dan VST3 32-bit berjalan lewat bridge (`LYNX-bridge32.exe`). Bridge tidak menambah latensi. Plugin yang crash di dalamnya berhenti sendiri sementara LYNX SHARE tetap memutar. Editor plugin yang dibridge terbuka di jendelanya sendiri. Jika build 32-bit dan 64-bit dari sebuah plugin sama-sama terinstal, `Ukuran bit` di `Pengaturan wrapper` jendela plugin memilih mana yang dimuat.

## Monitor performa plugin

![Monitor performa plugin: satu baris per instrumen dan efek beserta porsi waktu pemrosesannya](/assets/img/wiki/plugin-monitor.webp)

`Alat › Monitor performa plugin...` mencantumkan setiap instrumen dan efek beserta waktu pemrosesan yang dipakainya: saat ini, rata-rata, dan puncaknya. 100% berarti satu inti prosesor sibuk selama audio berlangsung.

| Pengaturan | Fungsinya |
|---|---|
| `Cari` | Menyaring berdasarkan plugin, channel, atau insert |
| `Hanya plugin` | Menyembunyikan instrumen dan efek milik LYNX SHARE sendiri |
| `Bekukan` | Menghentikan pembaruan angka |
| `Reset saat diputar` | Memulai rata-rata dan puncak dari awal setiap kali pemutaran dimulai |
| `Reset` | Menghapus rata-rata dan puncak sekarang |

## Saat plugin tidak terinstal

LYNX SHARE lebih dulu mencari plugin yang sama lewat pengenalnya, lalu lewat file, lalu lewat nama. Proyek yang disimpan dengan VST2 terbuka dengan VST3 dari produk yang sama, dan sebaliknya, tetapi plugin dimainkan dengan pengaturan default.

Jika sebuah efek sama sekali tidak terinstal, efek LYNX SHARE dengan peran yang sama mengambil alih slotnya dan aslinya tetap ada di proyek. Lihat [Instrumen dan efek bawaan](/wiki/built-in/). `Bantuan › File hilang...` mencantumkan plugin yang diganti.

## Perlu diketahui

- Jendela plugin yang dijalankan punya bilah preset: program milik plugin itu sendiri, presetmu, `Simpan preset...`, `Muat file preset...`, dan `Ekspor file preset...`. File `.fxp`, `.fxb`, dan `.vstpreset` hanya bisa dimuat ke plugin yang berjalan di dalam LYNX SHARE, bukan di proses terpisah.
- Monitor hanya mengukur selama jendelanya terbuka.
- Instrumen yang hilang belum diganti. Plugin instrumen yang hilang menjadi synth bawaan yang dipilih berdasarkan nama channel.

## Terkait

- [Instrumen dan efek bawaan](/wiki/built-in/)
- [Browser](/wiki/browser/)
- [Mixer](/wiki/mixer/)
- [Pemecahan masalah](/wiki/troubleshooting/)
