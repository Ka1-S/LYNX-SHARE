---
source: a6d9063ec3
---
LYNX SHARE adalah DAW gratis dan open-source untuk Windows, macOS, dan Linux. Cara kerjanya berbasis pattern: kamu memprogramnya di Channel Rack dan Piano Roll, menyusunnya di Playlist, dan memixnya di Mixer. Program ini membuka proyek FL Studio (`.flp`) dan set Ableton Live (`.als`), dan menyimpan format `.lynx` miliknya sendiri.

Ini masih alpha. Harap maklum jika ada bug, dan simpan sesering mungkin.

## Menjalankannya

1. Periksa tabel di bawah untuk memastikan sistemmu didukung.
2. Unduh dan instal. [Instalasi](/wiki/install/) berisi langkah untuk tiap sistem.
3. Jalankan. Wizard pengaturan singkat memilih bahasa, menguji output audio, dan mencantumkan folder plugin, yang lalu dipindai di latar belakang. Kamu bisa melewatinya dan membukanya lagi nanti lewat `Opsi › Wizard penyiapan (bahasa, audio, folder plugin)...`.
4. Jika kamu tidak mendengar apa-apa, buka [Pengaturan audio](/wiki/audio-setup/).
5. Baca [Tur jendela](/wiki/tour/), lalu buat [Lagu pertamamu](/wiki/first-song/).

## Yang kamu butuhkan

| Sistem | Persyaratan |
|---|---|
| Windows | Windows 10 atau 11, 64-bit |
| macOS | macOS 11 Big Sur atau lebih baru, Intel atau Apple silicon |
| Linux | x86_64 dengan glibc 2.35 atau lebih baru (Ubuntu 22.04+, Debian 12+, Fedora 36+, Arch atau Manjaro terbaru), X11 atau XWayland |

Kamu tidak perlu audio interface. LYNX SHARE memutar lewat perangkat suara yang sudah dipakai sistemmu. Kamu juga tidak perlu FL Studio. Jika FL Studio terinstal di komputer yang sama, LYNX SHARE menemukan sample-nya dan memakai file plugin yang sama untuk proyek `.flp`.

Plugin pihak ketiga bersifat opsional. LYNX SHARE punya instrumen dan efeknya sendiri, tercantum di [katalog](/instruments-and-effects/).

## Menyimpan pekerjaan

`Ctrl+S` menyimpan, `Ctrl+Shift+S` menyimpan dengan nama baru, dan `Ctrl+Alt+S` menyimpan versi baru (Song_2, Song_3, dan seterusnya). Ada autosave, yang bisa dijangkau lewat `File › Buka folder simpan otomatis` dan `File › Kembalikan ke cadangan terakhir...`.

`File › Baru dari template` punya empat titik awal: Empty, Basic beat, Trap with 808, dan House.

## Proyek demo

Installer dan zip Windows, `.dmg` macOS, dan `.tar.gz` Linux menyertakan proyek demo, `Demo.lynx`. Buka dengan `File › Buka...` (`Ctrl+O`). Di Windows file itu ada di folder instalasi, di samping `LYNX.exe`.

Kamu juga bisa membuatnya dari terminal. Lihat [Command line](/wiki/command-line/).

```
LYNX.exe --showcase out.lynx
```

## Selanjutnya ke mana

| Kamu ingin | Baca |
|---|---|
| Mempelajari tiap jendela | [Channel Rack](/wiki/channel-rack/), [Piano Roll](/wiki/piano-roll/), [Playlist](/wiki/playlist/), [Mixer](/wiki/mixer/), [Browser](/wiki/browser/) |
| Membuka proyek dari program lain | [Pindahan dari FL Studio](/wiki/coming-from-fl-studio/), [Pindahan dari Ableton Live](/wiki/coming-from-ableton/) |
| Memakai plugin VST, CLAP, LV2, atau AU milikmu | [Plugin](/wiki/plugins/) |
| Mencari sebuah tombol | [Pintasan](/wiki/shortcuts/) |
| Memperbaiki masalah | [Pemecahan masalah](/wiki/troubleshooting/) dan [FAQ](/faq/) |

## Perlu diketahui

- File programnya bernama `LYNX.exe` di Windows dan `LYNX` di macOS dan Linux. Folder pengaturannya juga bernama `LYNX`.
- Di Windows dan macOS, peluncuran pertama bisa menampilkan peringatan sistem, karena build Windows belum ditandatangani kodenya dan build macOS belum dinotarisasi. [Instalasi](/wiki/install/) menjelaskan apa yang harus diklik.
- Antarmuka berbahasa Inggris secara default pada sistem yang bahasanya tidak dimiliki LYNX SHARE. Kamu bisa mengubahnya di `Opsi › Pengaturan umum...`. Bahasa baru berlaku setelah restart.
- Paket FLEX di proyek FL Studio dienkripsi dan tidak pernah dibuka. Channel-nya mendapat patch synth yang dipilih berdasarkan nama preset.

## Terkait

- [Instalasi](/wiki/install/)
- [Pengaturan audio](/wiki/audio-setup/)
- [Tur jendela](/wiki/tour/)
- [Lagu pertamamu](/wiki/first-song/)
- [Unduh](/download/)
