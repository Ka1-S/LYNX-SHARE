---
source: e5206a071d
---
Setiap judul di bawah adalah satu masalah. Jika masalahmu tidak ada di sini, laporkan seperti dijelaskan di bagian akhir.

## Memulai program

### Windows: SmartScreen memperingatkan pada peluncuran pertama

File eksekusinya belum ditandatangani kodenya, jadi SmartScreen mungkin memperingatkan saat pertama kali kamu menjalankannya.

1. Klik `More info`.
2. Klik `Run anyway`.

Installer tidak membutuhkan hak admin.

### macOS: "tidak dapat dibuka"

Aplikasi ini tidak dinotarisasi oleh Apple dan tidak punya tanda tangan Developer ID, jadi peluncuran pertama butuh satu langkah tambahan.

1. Seret LYNX SHARE ke `Applications`.
2. Klik kanan (Control-klik) dan pilih `Buka`.
3. Klik `Buka` sekali lagi.

Di macOS 15 Sequoia atau lebih baru, coba buka sekali, lalu masuk ke `System Settings › Privacy & Security` dan klik `Open Anyway`. Atau, di Terminal:

```
xattr -dr com.apple.quarantine /Applications/LYNX.app
```

macOS meminta akses mikrofon saat pertama kali kamu merekam.

### Linux: AppImage tidak mau jalan

LYNX SHARE membutuhkan sistem x86_64 dengan glibc 2.35 atau lebih baru (Ubuntu 22.04, Debian 12, Fedora 36, Arch atau Manjaro terbaru) dan X11 atau XWayland.

1. Buat file itu bisa dieksekusi dan jalankan, dengan dua perintah di bawah.
2. Jika masih belum jalan, instal libfuse2. Paketnya `libfuse2` di Ubuntu 22.04, `libfuse2t64` di Ubuntu 24.04, `fuse2` di Arch, dan `fuse-libs` di Fedora.
3. Jika kamu lebih suka tidak memasangnya, unduh `.tar.gz`, ekstrak, lalu jalankan `./install.sh`.

```
chmod +x LYNX-*.AppImage
./LYNX-*.AppImage
```

`./install.sh` menginstal LYNX SHARE untuk penggunamu di bawah `~/.local` beserta entri menu. `./install.sh --uninstall` menghapusnya.

## Suara

### Tidak ada suara, atau perangkat audio salah

1. Buka `Opsi › Pengaturan audio...`.
2. Pilih driver.
3. Pilih perangkat output.

| Sistem | Driver |
|---|---|
| Windows | Windows Audio (WASAPI), DirectSound, ASIO, dan driver LYNX ASIO bawaan |
| macOS | Core Audio |
| Linux | PipeWire (native), JACK, dan ALSA. Pada awal pertama LYNX SHARE membuka yang pertama di antaranya yang punya perangkat |

ASIO adalah model driver Windows dan hanya ada di Windows. `Opsi › Wizard penyiapan (bahasa, audio, folder plugin)...` juga menguji perangkat audio. Lihat [Pengaturan audio](/wiki/audio-setup/).

### Linux: suara terputus-putus

Thread audio sebaiknya berjalan dengan prioritas real-time.

1. Buka `Opsi › Pengaturan audio...` dan baca `Status`. Di situ terlihat apakah thread mendapat prioritas real-time.
2. Jika tidak, instal rtkit (`sudo apt install rtkit`, `sudo dnf install rtkit`, atau `sudo pacman -S rtkit`), atau tambahkan dirimu ke grup `audio` dengan batas real-time, seperti pada setup JACK.
3. Keluar dan masuk lagi.

Detailnya ada di [LINUX.md](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/LINUX.md).

### Suara berderak atau putus

LYNX SHARE menghitung setiap dropout (buffer underrun) dan menyebutkan penyebabnya. Arahkan kursor ke meter `CPU` di toolbar, atau buka `Alat › Monitor performa plugin...`. Angka merah di samping persentase CPU adalah jumlahnya.

Petunjuk menyebutkan penyebab dropout terakhir:

| Penyebab | Artinya | Yang bisa dicoba |
|---|---|---|
| Pemrosesan lebih lama dari buffer | Satu blok audio butuh waktu hitung lebih lama dari durasinya. Petunjuk menyebutkan channel atau efek paling lambat di blok itu | Perbesar ukuran buffer di `Opsi › Pengaturan audio...`, bekukan channel itu, atau turunkan kualitas plugin |
| Pemrosesan mendekati batas buffer | Beban mendekati 100 % di sekitar dropout | Perbesar ukuran buffer, atau bekukan channel yang paling berat |
| Latensi driver atau sistem | Driver atau sistem terlambat padahal beban tetap rendah. Tidak ada plugin yang menyebabkannya | Perbesar ukuran buffer, coba driver lain (di Windows, driver ASIO antarmuka), dan di Linux periksa prioritas real-time (di atas) |

Setiap dropout juga ditulis ke `audio-glitches.log` di folder pengaturan, beserta perangkat, buffer, dan channel terberat.

## Plugin

### Plugin crash

Sebagian besar crash plugin hanya mematikan plugin itu: efek di-bypass, instrumen menjadi senyap, dan LYNX SHARE serta proyek tetap berjalan. Pesan `Plugin crash` lalu menawarkan:

- `Muat ulang`, yang memulai plugin lagi dengan pengaturan dari penyimpanan atau cadangan terakhir.
- `Selalu jalankan <plugin> di proses terpisah`, yang memulainya lagi di prosesnya sendiri, sekarang dan di setiap proyek. Crash di sana tidak bisa menjatuhkan LYNX SHARE. Plugin di proses terpisah membuka jendelanya sendiri (tidak bisa di-dock).

Jendela plugin menampilkan dua tombol yang sama sebagai ganti editornya. `Opsi › Pengelola plugin...` mencantumkan plugin yang selalu berjalan di proses terpisah di kolom `Proses terpisah`; klik kotaknya untuk menyalakan atau mematikannya.

Untuk menjalankan setiap plugin di prosesnya sendiri:

1. Buka `Opsi › Pengaturan umum...`.
2. Nyalakan `Jalankan plugin di proses terpisah (melindungi LYNX SHARE dari crash)`.
3. Buka ulang proyek.

Plugin yang crash atau berhenti merespons di prosesnya sendiri dibisukan. Simpan proyek dan buka ulang untuk memuat plugin itu lagi. Lihat [Plugin pihak ketiga](/wiki/plugins/).

### Plugin tidak ada di daftar

1. Pilih `Opsi › Pindai plugin`.
2. Jika masih belum muncul, buka `Opsi › Pengelola plugin...`, klik `Folder plugin...`, dan tambahkan folder plugin itu.
3. Lihat tab `Gagal`. File plugin yang gagal dipindai dilewati. Di Windows alasannya ada di `%APPDATA%\LYNX\scan-failures.log`.
4. Klik `Pindai ulang semua...` untuk mencoba lagi file yang gagal.

### Proyek memakai plugin yang tidak terinstal

Efek diganti dengan efek LYNX SHARE yang berperan sama, dan pengaturannya tetap disimpan di proyek. `Bantuan › File hilang...` mencantumkan apa yang hilang. Lihat [Instrumen dan efek bawaan](/wiki/built-in/).

### Video tidak bekerja di Linux

Track video dan `Export with video` membutuhkan FFmpeg. LYNX SHARE menjelaskan cara menginstalnya.

## Laporan crash

![Pertanyaan laporan crash yang tampil pada awal berikutnya, dengan Send, Show what will be sent, dan Don't send](/assets/img/wiki/crash-report.webp)

Laporan crash bersifat opt-in. Setelah crash atau freeze, awal berikutnya menanyakan apakah laporan akan dikirim.

| Perintah | Fungsinya |
|---|---|
| `Tampilkan yang akan dikirim` | Menampilkan teks persisnya |
| `Laporkan di GitHub saja` | Membuka formulir issue GitHub, sudah terisi. Kamu melampirkan filenya sendiri |
| `Buka folder berisi laporan` | Menampilkan file laporan di komputermu |
| `Include file names (projects, samples)` | Jika mati, hanya ekstensi setiap file yang dikirim |
| `Selalu kirim tanpa bertanya` | Mengirim laporan berikutnya tanpa bertanya |

`Laporan crash` di `Opsi › Pengaturan umum...` mengatur `Ask` (default), `Selalu kirim`, atau `Jangan pernah kirim`.

Laporan berisi versi LYNX SHARE, sistem operasi, CPU, kartu grafis beserta drivernya, ukuran layar, driver dan perangkat audio, file crash atau freeze, 60 langkah terakhir sesi, serta nama dan versi plugin pihak ketiga yang dimuat.

Laporan tidak berisi audio, proyek, sample, nama komputermu, atau nama penggunamu. Server tidak menyimpan alamat IP. Id yang terkirim dicantumkan di `crash-reports\sent.log` di folder pengaturan. Agar laporan dihapus, buka issue dengan id-nya. Teks lengkapnya ada di [kebijakan privasi](https://github.com/Ka1-S/LYNX-SHARE/blob/main/PRIVACY.md).

## Melaporkan masalah

![Jendela Report a problem: kotak deskripsi, alamat e-mail opsional, dan tombol Send](/assets/img/wiki/report-problem.webp)

1. Pilih `Bantuan › Laporkan masalah...`.
2. Jelaskan apa yang terjadi. Alamat e-mail bersifat opsional, untuk jika kamu ingin mendapat jawaban.
3. Biarkan `Lampirkan file crash / freeze terbaru dan langkah sesi ini` tetap tercentang jika masalahnya berupa crash.
4. Klik `Tampilkan yang akan dikirim` untuk memeriksa teksnya.
5. Klik `Kirim`. Tidak ada yang dikirim sebelum itu.

Kamu juga bisa membuka issue di [github.com/Ka1-S/LYNX-SHARE/issues](https://github.com/Ka1-S/LYNX-SHARE/issues).

## Perlu diketahui

- Setelah crash, salinan cadangan proyekmu ada di folder autosave LYNX SHARE.
- Jika sebuah plugin terlibat dalam crash, pertanyaan itu menawarkan untuk mulai sekarang menjalankan plugin tersebut di proses terpisah.
- Folder pengaturan adalah `%APPDATA%\LYNX` di Windows, `~/Library/Application Support/LYNX` di macOS, dan `~/.config/LYNX` di Linux.

## Terkait

- [Pengaturan audio](/wiki/audio-setup/)
- [Plugin pihak ketiga](/wiki/plugins/)
- [Instalasi](/wiki/install/)
- [FAQ](/faq/)
- [Perlindungan crash](/why/#crash)
