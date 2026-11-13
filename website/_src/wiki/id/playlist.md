---
source: 1b54c6493f
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
- Menu clip audio mengikuti layout FL Studio dalam dua kolom. Kolom kiri berisi bagian `Clip audio` (`Pratinjau`, `Dibisukan`, `Ganti nama dan warna...`, `Ubah warna...`, `Warna acak`, `Ubah ikon...`, `Pilih channel sumber`, `Pengaturan channel`, `Jadikan unik`, `Pilih semua klip serupa`, `Delete`, `Properti...`) dan bagian `Region` (`Pilih region`, `Cacah`). Kolom kanan berisi `Sample` (`Jadikan unik sebagai sampel`, `Edit sampel`, `Ekstrak stem dari sample`, `Deteksi tempo`, `Sesuaikan ke tempo`), `Otomasi` (`Otomatiskan › Panning` atau `Volume`, `Crossfade dengan`) dan `Fade` (`Atur ulang fade`). Item milik LYNX SHARE sendiri, seperti `Pisah di sini`, `Balik`, `Normalisasi`, dan `Konversi ke MIDI`, ada di bagian akhir, di bawah sebuah garis.
- `Otomatiskan › Panning` atau `Volume` membuat clip otomasi sepanjang clip audio, pada track-nya, untuk pan atau volume penyesuaian Levels channel.
- Tahan `Alt` saat menyeret untuk mengabaikan snap.
- Clip audio punya pegangan fade dan gain clip yang bisa kamu seret ke atas atau bawah. Klik kanan pegangan fade untuk menunya, dengan `Buat crossfade`. Klik kanan pegangan gain untuk `Reset gain` dan `Normalisasi`.
- `Shift+M` mengganti Stretch: mengubah ukuran clip audio lalu meregangkan audionya, bukan memotongnya.
- `Shift+Left` dan `Shift+Right` memindahkan clip terpilih satu langkah snap, `Shift+Up` dan `Shift+Down` satu track. `Alt+Left` dan `Alt+Right` memindahkannya satu piksel layar pada zoom saat ini, tanpa snap. Jika tidak ada clip yang dipilih, semua clip bergeser.
- `Jadikan unik sebagai sampel` di menu clip audio memberi clip itu file baru yang berisi apa yang ia putar, dengan pitch, stretch, dan reverse-nya sudah ada di dalam audio.
- Clip audio yang punya warp marker menampilkannya sebagai bendera kecil di tepi atasnya. Seret bendera ke samping untuk memindahkan marker.

## Menu Edit dan Select

Menu Playlist punya submenu `Edit` dan `Pilih`. `Edit` berisi perintah clip: potong, salin, dan tempel, `Geser ke kiri`, `Geser ke kanan`, `Geser ke atas`, `Geser ke bawah`, `Jadikan unik`, `Jadikan unik sebagai sampel`, `Sisipkan ruang`, `Hapus ruang`, perintah penggabungan, dan opsi fade. `Pilih` punya `Pilih menurut sumber terpilih` (`Shift+C`), `Balik pilihan` (`Shift+I`), `Batalkan pilihan` (`Ctrl+D`), clip yang dibisukan dan yang tumpang tindih, serta `Laso ajaib`. Dengan `Laso ajaib` menyala, gerakkan mouse melingkar mengelilingi clip, tanpa menekan tombol, untuk memilihnya.

## Track

Di header track, klik memilih, `Ctrl`+klik menambah atau mencabut, `Shift`+klik memilih satu rentang, dan klik kanan membuka menu track. `Shift`+roda mouse menggeser track ke atas atau bawah. Lampu track me-mute-nya (klik) atau men-solo-nya (`Ctrl`+klik atau klik kanan). Seret pembagi di antara dua track untuk mengubah ukuran salah satunya, atau `Shift`+seret untuk mengubah ukuran semuanya.

`Tampilan` di menu Playlist memilih apa yang ditampilkan header track: `Tampilkan kontrol pada trek audio`, `Tampilkan level pada trek audio`, dan `Tampilkan level pada trek instrumen`.

`Mode track` di menu track menjadikan sebuah track `Belum ditetapkan`, `Track audio`, atau `Track instrumen`, seperti di FL Studio. `Track audio` mencantumkan `Trek mixer baru` terlebih dahulu lalu setiap insert mixer menurut nomor dan nama. Pilihan berlaku untuk semua track terpilih. `Track instrumen` punya `Gunakan channel yang ada`, `Plugin lainnya...`, dan daftar instrumen Channel Rack.

Tanda `+` di kiri atas header track membuka pilihan yang sama untuk track baru: `Tambah trek audio`, `Plugin lainnya...`, dan instrumen. Klik kanan pada `+` langsung menambahkan track audio.

## Pattern Blocks

Dengan `Blok pattern (proyek saat ini)` menyala di menu Playlist, sebuah bagian di bawah track berisi satu baris untuk tiap pattern. Di sana, Pencil menaruh satu blok dan menyeretnya di sepanjang barisnya, Brush melukis blok, dan Eraser menghapusnya. Klik kanan menghapus dengan alat apa pun.

## Penggaris, loop, dan marker

- Klik penggaris untuk mengatur posisi song.
- Seret dengan klik kanan, `Ctrl`+seret, atau klik-dua-kali-lalu-seret di penggaris untuk membuat pilihan waktu, yang sekaligus menjadi loop. `Shift`+seret memindahkannya.
- `Ctrl+Ins` menyisipkan sepanjang pilihan waktu di semua track, `Ctrl+Del` menghapusnya.
- `Alt+T` menambahkan marker. Klik sebuah marker untuk melompat ke sana.
- `Ctrl`+roda mouse untuk zoom, `Alt`+roda mouse mengubah tinggi track.

## Otomasi

Klik kanan knob atau fader dan pilih `Buat clip otomasi`. Clip muncul di Playlist. Dengan Pensil, seret titik-titiknya, dan klik di kurva untuk menambah satu titik. Lajur otomasi di bawah track bersifat opsional dan mati secara default. Lihat [Otomasi](/wiki/automation/).

Klik kanan sebuah titik untuk menunya. Daftar `Mode`-nya berisi 13 mode kurva FL Studio untuk segmen yang berakhir di titik itu: `Halus`, `Hold`, tiga kurva tunggal, tiga kurva ganda, `Setengah sine`, `Tangga`, `Tangga halus`, `Pulse`, dan `Gelombang`. Titik baru memakai mode yang terakhir dipakai. Mode dibaca dari dan ditulis ke file .flp.

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
| Menggeser clip | `Shift+Left` / `Shift+Right` | Memindahkan clip terpilih satu langkah snap |
| Menggeser ke track lain | `Shift+Up` / `Shift+Down` | Memindahkan clip terpilih satu track |
| Menggeser halus clip | `Alt+Left` / `Alt+Right` | Memindahkan clip terpilih satu piksel layar, tanpa snap |
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
- Seperti di FL Studio, sebuah pattern dimainkan dari satu clip pada satu waktu. Saat clip pattern yang sama berikutnya dimulai, di track mana pun, clip sebelumnya tidak memulai not lagi dari situ. Clip dari pattern berbeda diputar bersamaan. Ini berlaku di proyek baru dan proyek FL Studio. Proyek .lynx yang disimpan sebelum 0.1.27 tetap memutar setiap clip secara penuh.
- Tempo yang digeser oleh kontroler MIDI yang tertaut tidak mengubah ukuran clip audio.
- Di Linux, video membutuhkan FFmpeg.

## Terkait

- [Channel Rack](/wiki/channel-rack/)
- [Otomasi](/wiki/automation/)
- [Merekam](/wiki/recording/)
- [Ekspor](/wiki/exporting/)
- [Tata letak](/wiki/layouts/)
