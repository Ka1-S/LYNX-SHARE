---
source: 8a120ab7de
---
Setiap rilis punya build untuk Windows, macOS, dan Linux di [halaman rilis](https://github.com/Ka1-S/LYNX-SHARE/releases). File-file itu dibangun dari source bertag oleh GitHub Actions dan dicantumkan beserta SHA-256-nya di `SHA256SUMS.txt`. Halaman [Unduh](/download/) menaut ke yang terbaru.

## Instal di Windows

Windows 10 atau 11, 64-bit.

1. Unduh `LYNX-<version>-setup-x64.exe`.
2. Jalankan. Ia hanya terinstal untuk penggunamu dan tidak membutuhkan hak admin.
3. SmartScreen mungkin memperingatkan, karena file eksekusinya belum ditandatangani kodenya. Pilih `More info`, lalu `Run anyway`.
4. Jalankan LYNX SHARE dari menu Start atau pintasan desktop.

Installer menjadikan LYNX SHARE program yang membuka proyek `.lynx`. Ia menawarkan LYNX SHARE di bawah *Buka dengan* untuk file `.flp` tanpa mengubah default FL Studio.

Ada dua cara lain:

| Cara | File atau tempat | Fungsinya |
|---|---|---|
| Zip portabel | `LYNX-<version>-windows-x64.zip` | Ekstrak di mana saja dan jalankan `LYNX.exe`. Tidak ada yang diinstal. |
| Microsoft Store | [LYNX SHARE di Store](https://apps.microsoft.com/detail/9NQRFBFDCX6N) | Store menginstalnya dan menjaganya tetap mutakhir. |

## Instal di macOS

macOS 11 Big Sur atau lebih baru. Satu `LYNX-<version>-macos-universal.dmg` universal mencakup Intel dan Apple silicon.

1. Buka `.dmg` dan seret LYNX SHARE ke *Aplikasi*.
2. Aplikasi ini tidak dinotarisasi oleh Apple dan tidak punya tanda tangan Developer ID, jadi peluncuran pertama butuh langkah tambahan. Klik kanan (atau Control-klik) LYNX SHARE di *Aplikasi*, pilih *Buka*, lalu *Buka* sekali lagi.
3. Di macOS 15 Sequoia atau lebih baru, coba buka sekali, lalu masuk ke *Pengaturan Sistem › Privasi & Keamanan* dan klik *Tetap Buka*.

Alternatif lewat Terminal:

```
xattr -dr com.apple.quarantine /Applications/LYNX.app
```

macOS meminta akses mikrofon saat pertama kali kamu merekam.

## Instal di Linux

x86_64 dengan glibc 2.35 atau lebih baru (Ubuntu 22.04+, Debian 12+, Fedora 36+, Arch atau Manjaro terbaru). LYNX SHARE membutuhkan X11 atau XWayland. Audio lewat PipeWire, JACK, atau ALSA, dan tidak ada yang perlu diinstal tambahan untuk itu.

AppImage (`LYNX-<version>-x86_64.AppImage`):

```
chmod +x LYNX-*.AppImage
./LYNX-*.AppImage
```

Beberapa distribusi membutuhkan libfuse2 untuk menjalankan AppImage (`libfuse2` di Ubuntu 22.04, `libfuse2t64` di 24.04, `fuse2` di Arch, `fuse-libs` di Fedora). Pada awal pertama, wizard pengaturan menawarkan untuk menambahkan LYNX SHARE ke menu aplikasi dan membuka file `.lynx` dengannya. Kamu bisa mengubahnya nanti di `Opsi › Pengaturan umum...`, atau dari terminal dengan `./LYNX-*.AppImage --menu-entry on` (atau `nonaktif`).

Jika kamu lebih suka tidak memasang libfuse2, pakai `LYNX-<version>-linux-x86_64.tar.gz`: ekstrak lalu jalankan `./install.sh`. Itu menginstal LYNX SHARE untuk penggunamu di bawah `~/.local` beserta entri menu, ikon, dan jenis file `.lynx`.

## Lokasi penyimpanan pengaturan

| Sistem | Folder pengaturan |
|---|---|
| Windows | `%APPDATA%\LYNX` |
| macOS | `~/Library/Application Support/LYNX` |
| Linux | `~/.config/LYNX` (atau di bawah `$XDG_CONFIG_HOME`) |

Folder itu menyimpan opsimu, bahasa yang dipilih (`language.txt`), pintasanmu (`keys.json`), folder pluginmu (`plugin-folders.txt`), dan daftar plugin yang telah dipindai (`plugins.xml`).

## Pembaruan

Saat dijalankan, LYNX SHARE menanyakan nomor versi terbaru ke GitHub. Tidak ada data tentangmu yang dikirim. Ketika versi baru rilis, sebuah bilah menampilkan `LYNX SHARE <version> is available` dengan tombol `Yang baru` dan tombol pembaruan. `Bantuan › Periksa pembaruan...` menanyakannya kapan saja.

| Build | Cara memperbarui |
|---|---|
| Installer Windows | `Perbarui` mengunduh installer baru, menanyakan perubahan yang belum disimpan, menginstal, dan memulai ulang |
| Zip portabel Windows, macOS | `Perbarui` mengunduh file baru dan menampilkannya di foldernya |
| AppImage Linux | `Perbarui` memperbarui file AppImage di tempat. Gear Lever dan AppImageUpdate bisa melakukan hal yang sama dari luar |
| `.tar.gz` Linux | `Unduh...` membuka halaman unduhan |
| Microsoft Store | Store yang memperbaruinya. Build ini tidak punya pemeriksaan pembaruan sendiri |

Untuk mematikan pemeriksaan itu, buka `Opsi › Pengaturan umum...` dan hapus centang `Periksa pembaruan secara otomatis`.

## Menghapus

- Installer Windows: copot LYNX SHARE dari daftar *Aplikasi & fitur* di Windows.
- Zip portabel: hapus folder hasil ekstrak.
- `.tar.gz` Linux: jalankan `./install.sh --uninstall`.

## Perlu diketahui

- Build Windows tidak bertanda tangan sampai permohonan penandatanganan kode proyek ini diterima. Aplikasi macOS hanya punya tanda tangan ad-hoc. Paket Linux tidak ditandatangani.
- Untuk memeriksa unduhan, bandingkan SHA-256-nya dengan baris di `SHA256SUMS.txt` di halaman rilis.
- Installer, zip, `.dmg`, dan `.tar.gz` menyertakan proyek demo `Demo.lynx`.
- Menutup bilah pembaruan dengan tombol tutupnya melewati versi itu.

## Terkait

- [Mulai di sini](/wiki/getting-started/)
- [Pengaturan audio](/wiki/audio-setup/)
- [Pemecahan masalah](/wiki/troubleshooting/)
- [Unduh](/download/)
- [Pembaruan](/updates/)
