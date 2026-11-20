---
source: 2298a70fec
---
![Jendela Riwayat urungkan: enam langkah dari Start sampai Add notes, dengan Mute: Kick sebagai langkah saat ini](/assets/img/wiki/history.webp)

Setiap suntingan pada proyek adalah satu langkah dalam riwayat urungkan: not yang kamu tambah, clip yang kamu pindah, fader, perubahan routing, tempo. `Edit › Riwayat urungkan...` (`Ctrl+Alt+H`) menampilkan langkah-langkahnya sebagai daftar. Terpisah dari itu, LYNX SHARE menyimpan salinan cadangan proyek selama kamu bekerja.

## Kembali ke langkah sebelumnya

1. Pilih `Edit › Riwayat urungkan...`, atau tekan `Ctrl+Alt+H`.
2. Daftar menampilkan langkah dengan yang terbaru di bawah. Baris pertama, `Awal`, adalah proyek seperti saat dibuka.
3. Klik sebuah langkah. Proyek kembali ke keadaan tepat setelah langkah itu, dan langkah-langkah sesudahnya menjadi abu-abu.
4. Klik langkah yang abu-abu untuk maju lagi.
5. Lakukan suntingan baru untuk melanjutkan dari langkah yang kamu pilih. Langkah abu-abu itu lalu hilang.

## Tombol urungkan

`Ctrl+Z` mundur menelusuri riwayat, satu langkah per tekanan, seperti di FL Studio 2026 dan kebanyakan program. `Ctrl+Y` atau `Ctrl+Shift+Z` maju lagi.

| Perintah | Tombol | Fungsinya |
|---|---|---|
| `Urungkan` | `Ctrl+Z` | Mundur satu langkah per tekanan |
| `Ulangi` | `Ctrl+Y` atau `Ctrl+Shift+Z` | Maju satu langkah |
| `Riwayat urungkan...` | `Ctrl+Alt+H` | Membuka daftar |

Menu `Edit` menyebut langkah yang dikenai tiap item, misalnya `Undo Add notes: Pattern 1`. Setelah setiap urungkan atau ulangi, bilah petunjuk menyebut langkahnya dan menampilkan levelnya, misalnya `Level 2/3`.

Mode urungkan FL Studio yang lain juga ada: matikan `Mode urungkan alternatif` di `Opsi › Pengaturan umum...`. `Ctrl+Z` lalu mengurungkan langkah terakhir dan, kalau ditekan lagi, mengulanginya, jadi ia berganti antara dua keadaan terakhir. `Ctrl+Alt+Z` (`Urungkan mundur`) mundur lebih jauh, dan `Ctrl+Y` atau `Ctrl+Shift+Z` tetap mengulangi.

## Apa yang menjadi langkah dan apa yang bukan

- Suntingan pada proyek adalah langkah: not, pattern, clip, channel, level dan routing mixer, efek, otomasi, tempo, mute.
- Sebuah rekaman adalah langkah. Begitu pula menggabungkan sebuah bagian dalam [sesi online](/wiki/online-sessions/), dan `Buat comp dari take ini`.
- Hal yang tidak mengubah proyek bukan langkah: zoom, menggulir, memindahkan jendela, dan pengaturan program itu sendiri.
- Menyimpan dan mengekspor tidak diurungkan. File yang ditulisnya tetap ada.
- Jendela plugin bawaan LYNX SHARE punya urungkan dan ulangi terpisah untuk perubahan plugin itu, di samping pengaturan A dan B.

## Pengaturan

Keduanya ada di `Opsi › Pengaturan umum...`.

| Pengaturan | Fungsinya |
|---|---|
| `Tingkat urungkan maksimum` | Berapa banyak langkah yang disimpan: 25, 50, 100, 200, 500, atau 1000. Bawaannya 100 |
| `Mode urungkan alternatif` | Aktif secara bawaan: `Ctrl+Z` mundur menelusuri riwayat. Mati: `Ctrl+Z` berganti antara urungkan dan ulangi untuk langkah terakhir |

## Cadangan

LYNX SHARE menyimpan salinan cadangan proyek yang terbuka setiap 2 menit secara bawaan, dan menyimpan 10 yang terbaru per proyek. Cadangan adalah file `.lynx` biasa di folder `autosave` di dalam folder pengaturan, dinamai sesuai proyek dengan `(autosaved on ...)` serta tanggal dan jamnya.

Setiap kali kamu menyimpan menimpa file proyek yang sudah ada, versi yang tertimpa disalin ke folder yang sama dengan `(overwritten on ...)` pada namanya.

| Perintah | Fungsinya |
|---|---|
| `File › Kembalikan ke cadangan terakhir...` | Mengganti proyek yang terbuka dengan cadangan terbarunya, setelah bertanya. File proyeknya sendiri baru berubah saat kamu menyimpan |
| `File › Buka folder simpan otomatis` | Menampilkan cadangan di file manager |
| `File › Simpan versi baru` (`Ctrl+Alt+S`) | Menyimpan sebagai `Song_2`, `Song_3`, dan seterusnya, di sebelah file saat ini |

Bagian `Cadangan` di `Opsi › Pengaturan umum...` punya pengaturan berikut:

| Pengaturan | Fungsinya |
|---|---|
| `Simpan salinan cadangan` | `Tidak pernah`, atau interval dalam menit |
| `Cadangan yang disimpan per proyek` | 1, 3, 5, 10, 20, atau 50 |
| `Tunda cadangan saat lagu diputar atau keyboard MIDI sedang dimainkan` | Menahan cadangan supaya tidak mengganggumu |
| `Jeda terlama antar cadangan` | 5 sampai 60 menit. Cadangan dibuat setelah waktu ini walaupun kamu sedang bermain |

Kalau sesi terakhir berakhir tiba-tiba, saat dijalankan berikutnya program menawarkan untuk membuka cadangan terbaru yang disimpan sesi itu.

## Baik diketahui

- Riwayat tidak disimpan bersama proyek. Membuka proyek atau memulai yang baru mengosongkannya.
- Saat riwayat penuh, langkah tertua dibuang.
- Dalam sesi online live, urungkan dan ulangi hanya mengembalikan suntinganmu sendiri, tidak pernah suntingan orang lain.
- Dengan `Simpan salinan cadangan` pada `Tidak pernah`, salinan yang dibuat saat kamu menyimpan menimpa file tetap disimpan.
- Folder pengaturannya adalah `%APPDATA%\LYNX` di Windows, `~/Library/Application Support/LYNX` di macOS, dan `~/.config/LYNX` di Linux.

## Terkait

- [Pintasan](/wiki/shortcuts/)
- [Pengaturan](/wiki/settings/)
- [Format file](/wiki/file-formats/)
- [Pemecahan masalah](/wiki/troubleshooting/)
- [Yang hanya dilakukan LYNX SHARE: keamanan saat crash](/why/#crash)
