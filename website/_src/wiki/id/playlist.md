---
source: e767f4482b
---
![Playlist dengan clip di track](/assets/img/playlist.webp)

Playlist adalah tempat pattern menjadi sebuah song. Di track-nya ada tiga jenis clip: clip pattern, clip audio, dan clip otomasi. Tekan `F5` untuk menampilkan atau menyembunyikannya, dan `Enter` untuk memaksimalkan atau memulihkannya.

Transport punya dua mode. Mode Pattern memutar pattern saat ini dalam loop. Mode Song memutar Playlist. Tekan `L` untuk berpindah.

## Menaruh pattern di song

1. Tekan `F5` untuk menampilkan Playlist.
2. Di panel Pemilih, klik sebuah pattern. Pemilih mencantumkan pattern, audio, dan otomasi proyek, dan yang kamu klik menjadi sumber clip.
3. Tekan `P` untuk Pensil.
4. Klik di sebuah track untuk menaruh clip. Tahan tombol dan seret untuk mengatur posisinya.
5. Tekan `B` untuk Kuas dan seret sepanjang track untuk melukis clip yang bersambung ujung ke ujung.
6. Tekan `L` untuk mode Song, lalu `Space` untuk memutar.

File audio bisa diseret dari [Browser](/wiki/browser/) ke Playlist.

## Alat

![Toolbar Playlist dengan kotak aransemen, alat-alat, dan kotak snap](/assets/img/wiki/playlist-tools.webp)

Tombol hurufnya bekerja selama piano keyboard ketik (`Ctrl+T`) mati.

| Alat | Tombol | Fungsinya |
|---|---|---|
| Pensil | `P` | Klik menaruh clip dari Pemilih |
| Kuas | `B` | Seret melukis clip ujung ke ujung |
| Penghapus | `D` | Klik atau seret untuk menghapus |
| Mute | `T` | Klik atau seret untuk me-mute atau membatalkan mute clip |
| Pisau | `C` | Seret garis untuk memotong melintasi clip, klik memotong di titik itu |
| Laso | `E` | Seret menggambar kotak pilihan. `Shift` menambah |
| Zoom | `Z` | Klik memperbesar, seret sebuah rentang untuk memperbesar ke sana, klik kanan memperkecil |
| Dengarkan | `Y` | Klik atau seret untuk memutar dari titik itu |
| Slip | `S` | Seret menggeser isi clip di dalam clip |

## Bekerja dengan clip

- Seret clip untuk memindahkannya. `Shift`+seret membuat salinan.
- Seret ujung clip untuk mengubah ukurannya. Menyeret awalnya memangkas clip sementara isinya tetap di tempat.
- Klik kanan menghapus clip. `Ctrl`+seret memilih.
- Klik dua kali sebuah clip untuk membukanya.
- Tombol di kiri atas clip membuka menu clip (jadikan unik, ganti nama dan warna, cacah, dan untuk audio: pitch, sesuaikan ke tempo, balik).
- Tahan `Alt` saat menyeret untuk mengabaikan snap.
- Clip audio punya pegangan fade dan gain clip yang bisa kamu seret ke atas atau bawah.
- `Shift+M` mengganti Stretch: mengubah ukuran clip audio lalu meregangkan audionya, bukan memotongnya.

## Track

Di header track, klik memilih, `Ctrl`+klik menambah atau mencabut, `Shift`+klik memilih satu rentang, dan klik kanan membuka menu track. `Shift`+roda mouse menggeser track ke atas atau bawah. Lampu track me-mute-nya (klik) atau men-solo-nya (`Ctrl`+klik atau klik kanan). Seret pembagi di antara dua track untuk mengubah ukuran salah satunya, atau `Shift`+seret untuk mengubah ukuran semuanya.

## Penggaris, loop, dan marker

- Klik penggaris untuk mengatur posisi song.
- Seret dengan klik kanan, `Ctrl`+seret, atau klik-dua-kali-lalu-seret di penggaris untuk membuat pilihan waktu, yang sekaligus menjadi loop. `Shift`+seret memindahkannya.
- `Ctrl+Ins` menyisipkan sepanjang pilihan waktu di semua track, `Ctrl+Del` menghapusnya.
- `Alt+T` menambahkan marker. Klik sebuah marker untuk melompat ke sana.
- `Ctrl`+roda mouse untuk zoom, `Alt`+roda mouse mengubah tinggi track.

## Otomasi

Klik kanan knob atau fader dan pilih `Buat clip otomasi`. Clip muncul di Playlist. Dengan Pensil, seret titik-titiknya, dan klik di kurva untuk menambah satu titik. Lajur otomasi di bawah track bersifat opsional dan mati secara default. Lihat [Otomasi](/wiki/automation/).

## Aransemen

Kotak aransemen di toolbar berpindah antar aransemen. Sebuah proyek bisa punya beberapa.

## Tombol

| Perintah | Tombol | Hasilnya |
|---|---|---|
| Pilih semua | `Ctrl+A` | Memilih setiap clip |
| Salin, potong, tempel | `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | Menyalin, memotong, atau menempel clip |
| Duplikat | `Ctrl+B` | Menduplikasi pilihan |
| Hapus | `Del` | Menghapus clip terpilih |
| Hapus ripple | `Shift+Del` | Menghapus clip terpilih dan menutup celahnya |
| Ripple editing | `Shift+S` | Menyalakan atau mematikan ripple editing |
| Mute, batalkan mute | `Alt+M` / `Shift+Alt+M` | Me-mute atau membatalkan mute clip terpilih |
| Jadikan unik | `Ctrl+Shift+V` | Menjadikan clip terpilih unik |
| Gabungkan | `Ctrl+G` | Menggabungkan clip pattern |
| Stretch | `Shift+M` | Mengganti Stretch untuk mengubah ukuran clip audio |
| Sisipkan waktu | `Ctrl+Ins` | Menyisipkan sepanjang pilihan waktu di semua track |
| Hapus waktu | `Ctrl+Del` | Menghapus pilihan waktu di semua track |
| Tambah marker | `Alt+T` | Menambahkan marker |
| Zoom | `PageUp` / `PageDown` | Memperbesar atau memperkecil |
| Zoom agar pas | `Shift+4` | Zoom untuk menampilkan semuanya |
| Maksimalkan | `Enter` | Memaksimalkan atau memulihkan Playlist |

## Perlu diketahui

- Playlist hanya terdengar dalam mode Song. Dalam mode Pattern, `Space` memutar pattern saat ini, apa pun yang ada di track.
- `Tampilan › Gaya tata letak › Alur kerja peluncuran klip` menambahkan kisi clip dan scene di samping Playlist. Ini opsional. Lihat [Tata letak](/wiki/layouts/).
- Jika beberapa clip otomasi saling tumpang tindih pada satu kontrol, pilihan clip mana yang diputar bisa berbeda dari FL Studio (catatan rilis 0.1.25).
- Tempo yang digeser oleh kontroler MIDI yang tertaut tidak mengubah ukuran clip audio.
- Di Linux, video membutuhkan FFmpeg.

## Terkait

- [Channel Rack](/wiki/channel-rack/)
- [Otomasi](/wiki/automation/)
- [Merekam](/wiki/recording/)
- [Ekspor](/wiki/exporting/)
- [Tata letak](/wiki/layouts/)
