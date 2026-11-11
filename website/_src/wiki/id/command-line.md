---
source: 98829adcb8
---
LYNX SHARE juga bisa berjalan tanpa membuka jendelanya. Satu opsi merender proyek menjadi audio, opsi lain mengonversinya ke proyek FL Studio atau set Ableton Live, dan beberapa lagi ada untuk pengujian. Pemeriksaan milik proyek ini sendiri memakai opsi yang sama.

Programnya adalah `LYNX.exe` di Windows dan `LYNX` di macOS dan Linux. Di macOS path lengkapnya `/Applications/LYNX.app/Contents/MacOS/LYNX`. Opsinya sama di semua sistem.

## Merender proyek ke file

1. Buka terminal di folder tempat program berada. Di Windows itu folder instalasi atau folder tempat kamu mengekstrak zip.
2. Jalankan `LYNX.exe --export project.flp out.flac`, dengan proyek dan file output milikmu sendiri.
3. Tunggu sampai program keluar. Tidak ada jendela yang terbuka.
4. Baca `out.flac.txt`, yang ditulis di samping output. Untuk proyek hasil impor, file itu mencantumkan apa yang dikonversi, didekati, dan tidak ikut, serta plugin mana yang ditemukan atau hilang.

## Membuka proyek

```
LYNX.exe project.lynx
```

Memulai LYNX SHARE dengan jendelanya dan membuka file itu. Format yang diterima: `.lynx`, `.flp`, `.als`, `.flm`, dan `.mid`. Path relatif dihitung dari folder saat ini.

## Opsi untuk pemakaian sehari-hari

| Opsi | Fungsinya |
|---|---|
| `--export <project> <out>` | Merender seluruh song beserta tail-nya, seperti jendela Ekspor. Ekstensi `<out>` menentukan format: `.wav` (24-bit), `.flac`, `.ogg`, atau `.mp3` (320 kbps) |
| `--export ... --pattern` | Hanya merender pattern saat ini |
| `--export ... --insert N` | Hanya merender output insert Mixer N |
| `--export ... --rate N` | Merender dengan sample rate lain, dalam Hz |
| `--stems <project> <base.wav>` | Menulis satu WAV per insert Mixer yang punya channel: `base - 1.wav`, `base - 2.wav`, dan seterusnya |
| `--export-flp <project> <out.flp>` | Menulis proyek FL Studio. Dengan `out.zip` menulis paket loop yang di-zip |
| `--export-flp ... --substitute` | Menukar plugin khusus LYNX dengan plugin FL terdekat, bukan merendernya menjadi audio |
| `--export-als <project> <folder>` | Menulis set Ableton Live ke `<folder>/<name> Project/<name>.als`, untuk Live 12 |
| `--export-als ... --live11` | Menulis set untuk Live 11 |
| `--devices <report.txt>` | Mencantumkan jenis driver audio dan perangkat di komputer ini |
| `--showcase <out.lynx>` | Menulis proyek demo milik LYNX SHARE sendiri |
| `--lang <id>` | Memakai bahasa antarmuka lain untuk satu kali jalan, misalnya `--lang pt-BR` |
| `--menu-entry on` atau `nonaktif` | Khusus AppImage Linux: menambahkan LYNX SHARE ke menu aplikasi, atau menghapusnya |

`--export`, `--stems`, dan `--export-flp` menerima `.lynx`, `.flp`, atau `.als` sebagai proyek. `--export-als` juga menerima `.flm`.

Contoh:

```
LYNX.exe --export song.lynx song.wav
LYNX.exe --export song.lynx drums.wav --insert 1
LYNX.exe --stems song.lynx song.wav
LYNX.exe --export-flp song.lynx song.flp
LYNX.exe --export-als song.lynx out-folder --live11
LYNX.exe --devices devices.txt
LYNX --lang de
```

## Opsi untuk pengujian

Opsi ini ada untuk memeriksa LYNX SHARE itu sendiri. Berguna dalam laporan bug.

| Opsi | Fungsinya |
|---|---|
| `--selftest <report.txt> [demo.lynx]` | Menjalankan self-test tanpa perangkat audio. Laporan diakhiri `result=OK` jika lulus |
| `--snapshot <prefix> [project]` | Menggambar antarmuka dan jendelanya ke file PNG bernama sesuai prefix. Gambar-gambar di wiki ini dibuat dengan cara ini |
| `--render <project> <out.wav> <seconds>` | Merender sekian detik pattern saat ini ke WAV 24-bit |
| `--plugintest <plugin> <report.txt>` | Memuat dan menguji satu plugin VST3 atau CLAP |
| `--sandboxtest <plugin> <report.txt>` | Menguji plugin di proses terpisah, termasuk yang hang |
| `--scantest <folder> <report.txt>` | Memindai folder untuk mencari plugin, di proses terpisah |
| `--fldb <report.txt> [project.flp]` | Melaporkan instalasi FL Studio yang ditemukan di sini, database plugin-nya, dan kecocokan plugin sebuah proyek |
| `--devicetest <type> <device> <report.txt>` | Membuka driver audio selama satu detik keheningan dan melaporkannya |
| `--uiperf <project> <report.txt>` | Mengukur waktu penggambaran Mixer, Playlist, dan Channel Rack |

Contoh:

```
LYNX.exe --selftest report.txt
LYNX.exe --snapshot shots/s project.lynx
LYNX --devicetest PipeWire "System default" out.txt
```

## Perlu diketahui

- Hasilnya masuk ke file, bukan ke terminal. `--export` menulis `<out>.txt`, `--export-flp` menulis `<out.flp>.txt`, `--export-als` menulis `export-als.txt` di folder itu, dan `--stems` menulis `<base.wav>.txt`.
- Program membaca standard input. Dalam loop shell, tambahkan `< /dev/null` pada setiap pemanggilan.
- `--snapshot` menggambar pada 1280x720 kecuali variabel lingkungan `LYNX_SNAPSHOT_SIZE` menyatakan lain, misalnya `1920x1080`.
- `--lang` menerima id bahasa yang tercantum di `Opsi › Pengaturan umum...`, seperti `en`, `pt-BR`, `de`, `es`, `fr`, atau `ja`. Id yang tidak dikenal diabaikan.
- `--export` memakai jalur render yang sama dengan jendela Ekspor, tetapi tanpa opsi lain dari jendela itu. Untuk dither, bit depth, atau normalisasi, pakai jendelanya.
- Source punya jauh lebih banyak opsi pengujian. Itu internal dan bisa berubah antarversi.

## Terkait

- [Ekspor](/wiki/exporting/)
- [Format file](/wiki/file-formats/)
- [Build dari source](/wiki/build-from-source/)
- [Pemecahan masalah](/wiki/troubleshooting/)
