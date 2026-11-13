---
source: a73763934f
---
![Piano Roll](/assets/img/pianoroll.webp)

Piano Roll menampilkan not dari satu channel di pattern saat ini, dengan keyboard di kiri dan jalur nilai per not di bagian bawah. Tekan `F7` untuk menampilkan atau menyembunyikannya, atau pilih `Piano Roll` di menu channel di [Channel Rack](/wiki/channel-rack/).

Kotak channel di toolbar memilih channel mana yang kamu edit. Channel yang punya not di pattern ini diberi tanda. Roda di atas kotak itu berpindah antar channel yang punya not, `Ctrl`+roda melewati semuanya.

## Menulis beberapa not

1. Di Channel Rack, klik kanan nama channel dan pilih `Piano Roll`.
2. Tekan `P` untuk Pensil.
3. Klik di grid untuk menambah not. Tahan tombolnya dan seret untuk mengatur posisinya.
4. Seret ujung not untuk memanjangkan atau memendekkannya.
5. Klik kanan sebuah not untuk menghapusnya.
6. Tekan `Space` untuk mendengar pattern.

## Alat

![Toolbar Piano Roll dengan kotak channel, alat, dan kotak snap](/assets/img/wiki/pianoroll-tools.webp)

Setiap alat punya tombol huruf. Huruf-huruf itu berfungsi saat piano keyboard ketik (`Ctrl+T`) mati.

| Alat | Tombol | Fungsinya |
|---|---|---|
| Pensil | `P` | Klik menambah not, seret mengatur posisinya |
| Kuas | `B` | Seret melukis deretan not. Dengan `Shift`, satu per satu |
| Kuas drum | `N` | Satu pukulan per sel grid pada tuts di bawah mouse |
| Penghapus | `D` | Klik atau seret untuk menghapus |
| Mute | `T` | Klik atau seret untuk me-mute atau meng-unmute not |
| Pisau | `C` | Seret garis untuk memotong melintasi not, klik memotong di titik itu |
| Laso | `E` | Seret menggambar kotak seleksi. `Shift` menambah |
| Zoom | `Z` | Klik memperbesar, seret sebuah rentang untuk zoom ke sana, klik kanan memperkecil |
| Dengarkan | `Y` | Klik atau seret memainkan not di bawah kursor |

## Bekerja dengan not

- Seret sebuah not untuk memindahkannya. `Shift`+seret membuat salinan.
- Seret ujung not untuk mengubah ukurannya. Awalnya juga bisa diseret, dengan ujung akhir tetap di tempatnya.
- Klik kanan menghapus not, di alat apa pun.
- Klik dua kali sebuah not untuk membuka propertinya.
- `Alt`+roda di atas not mengubah velocity-nya.
- `Ctrl`+seret untuk memilih. `Ctrl`+klik menambahkan not ke seleksi atau menghapusnya.
- Tahan `Alt` saat menyeret untuk mengabaikan snap. `Backspace` mematikan dan menyalakan snap.
- Dengan beberapa not terpilih, handle stretch meregangkan atau memampatkannya dalam waktu.

## Keyboard dan penggaris

Klik sebuah tuts di keyboard untuk mendengarnya. Makin ke kanan pada tuts, makin keras. `Ctrl`+klik sebuah tuts memilih not-nya. Seret dengan tombol tengah pada keyboard untuk membuat tuts lebih tinggi atau lebih pendek. Roda zoom vertikal juga bisa, yaitu kotak kecil di ujung bar gulir waktu: seret ke atas atau bawah, atau pakai roda mouse di atasnya.

`Tampilan › Gaya keyboard` di menu Piano roll memilih antara `Tuts datar` dan `Tuts piano (garis di antara tuts putih)`, yang menggambar keyboard tradisional.

Klik penggaris untuk mengatur posisi. `Ctrl`+seret, seret dengan klik kanan, atau klik dua kali lalu seret pada penggaris memilih not dalam rentang waktu. `Ctrl`+roda untuk zoom dan `Shift`+roda untuk menggulir.

## Toolbar

| Pengaturan | Fungsinya |
|---|---|
| Snap | "Main" mengikuti snap di toolbar utama, atau pilih satu untuk jendela ini. |
| Chord | Mengklik membuat chord yang dipilih, dengan not yang diklik sebagai root. `Shift+N` kembali ke not tunggal. Daftar yang sama adalah `Stempel akor` di menu Piano roll. |
| Tangga nada | Menyorot not dari sebuah tangga nada. Tombol terpisah men-snap not baru dan not yang dipindahkan, serta MIDI masuk, ke tangga nada yang dipilih. Not yang dipindahkan lalu bergerak per langkah tangga nada, sehingga chord tetap berada di tangga nada, dan `Up` serta `Down` memindahkan satu langkah tangga nada. |
| Slide (`S`) | Not baru menjadi not slide. Not yang sedang berbunyi meluncur ke sana (glide 808). |
| Ghost note (`Alt+V`) | Menampilkan not channel lain di belakang notemu. |
| Warna not | Not baru mendapat warna ini. Ada 16. |
| Follow | Tampilan menggulir mengikuti playhead selama pattern diputar. |
| Kotak jalur | Memilih apa yang diedit jalur di bawah: Velocity, Pan, Fine pitch, Release, Mod X, Mod Y, Chance, atau Velocity range. |

## Chance dan velocity range

Setiap not punya Chance dan Velocity range. Chance adalah seberapa besar kemungkinan not itu diputar setiap kali gilirannya tiba. Velocity range membuat velocity-nya bervariasi antara velocity not dan velocity itu ditambah range-nya. Pilih `Peluang` atau `Rentang velocity (LYNX)` di kotak jalur untuk mengeditnya. `Alat › Peluang (LYNX)` di menu Piano roll mengelompokkan not terpilih agar semuanya atau hanya satu di antaranya yang diputar, dan punya `Atur ulang peluang dan rentang velocity`.

## Alat not

Menu `Alat` di Piano roll membuka alat not FL Studio sebagai dialog. Alat-alat ini bekerja pada not terpilih, atau pada semua not bila tidak ada yang dipilih. Beberapa di antaranya:

| Alat | Tombol | Fungsinya |
|---|---|---|
| Limit | `Alt+K` | Menggeser not per oktaf ke dalam rentang tuts, lalu membatasinya. Di bawah `Pembatasan kunci / tangga nada` ia menjaga not tetap pada satu tuts dasar dan salah satu chord atau tangga nada FL Studio, dengan `Snap` diatur ke `Di atas`, `Di bawah`, atau `Bergantian`. `Lipat ke atas / bawah` dan `Transpose` menentukan ke mana not di luar rentang pergi |
| Flip | `Alt+Y` | `Balik horizontal` mencerminkan not dalam waktu. Dengan `Pertahankan waktu mulai`, yang pada awalnya menyala, waktu mulai dicerminkan dan panjang not tetap. `Balik vertikal` mencerminkan tuts di sekitar titik tengah rentangnya |
| Randomise | `Alt+R` | Perubahan acak pada velocity, pan, fine pitch, timing, dan panjang, serta pada Mod X, Mod Y, dan release |
| Scale levels | `Alt+X` | Menskalakan level not dari awal sampai akhir seleksi, dengan `Offset`, `Tengah` (nilai yang menjadi pusat skala), dan `Tegangan` |
| LFO | `Alt+O` | Menulis bentuk LFO ke level not. Klik kanan slider `Periode` untuk daftar dari 1 step sampai 512 step |

## Alat MIDI dan skrip

`Alat › Alat MIDI...` di menu Piano roll membuka panel dengan Arpeggiate, Chop, Connect, Ornament, Quantize, Recombine, Span, Strum, Time Warp, dan Velocity Shaper.

Kolom `Skrip` di menu `Alat` mencantumkan skrip piano roll: milik LYNX SHARE sendiri dan file `.pyscript`, termasuk yang ada di folder skrip FL Studio jika FL Studio terinstal. `Buka folder skrip...` membuka folder untuk skrip milikmu sendiri, dan `Jalankan lagi skrip terakhir` mengulang skrip terakhir.

## Tombol

| Perintah | Tombol | Fungsinya |
|---|---|---|
| Pilih semua | `Ctrl+A` | Memilih setiap not |
| Batalkan pilihan | `Ctrl+D` atau `Esc` | Mengosongkan seleksi |
| Salin, potong, tempel | `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | Menyalin, memotong, atau menempel not |
| Duplikat | `Ctrl+B` | Menduplikasi seleksi |
| Hapus | `Del` | Menghapus seleksi |
| Transpose | `Up` / `Down` | Menggeser seleksi satu semiton |
| Transpose oktaf | `Ctrl+Up` / `Ctrl+Down` | Menggeser seleksi satu oktaf |
| Geser sedikit | `Left` / `Right` | Menggeser seleksi satu langkah grid |
| Kuantisasi cepat | `Ctrl+Q` | Mengkuantisasi awal dan panjang |
| Kuantisasi awal | `Shift+Q` | Mengkuantisasi awalnya saja |
| Cacah | `Ctrl+U` | Mencacah not ke grid |
| Rekatkan | `Ctrl+G` | Menggabungkan not yang bersentuhan |
| Legato | `Ctrl+L` | Membuat setiap not bertahan sampai not berikutnya |
| Mute, unmute | `Alt+M` / `Shift+Alt+M` | Me-mute atau meng-unmute seleksi |
| Impor MIDI | `Ctrl+M` | Mengimpor file MIDI ke channel ini |
| Ekspor MIDI | `Ctrl+Shift+M` | Mengekspor pattern sebagai file MIDI |
| Zoom | `PageUp` / `PageDown` | Memperbesar atau memperkecil |

Alat not (Arpeggio, Strum, Balik, Acak, Kuantisasi, Cacah, dan lainnya) membuka dialog. Tombolnya ada di halaman [Pintasan](/wiki/shortcuts/).

## Baik diketahui

- Piano Roll menampilkan satu channel dalam satu pattern. Ganti pattern saat ini dan ia menampilkan not pattern itu.
- Huruf alat tidak berfungsi saat piano keyboard ketik (`Ctrl+T`) menyala.
- Klik kanan menghapus dengan alat apa pun, jadi tidak perlu beralih ke Penghapus untuk satu not.
- `S` di sini adalah sakelar Slide. Di Playlist, huruf yang sama adalah alat Slip.
- Not yang bukan step biasa (tuts lain, not lebih panjang, slide) membuat baris channel di Channel Rack menampilkan miniatur, bukan tombol step.

## Terkait

- [Channel Rack](/wiki/channel-rack/)
- [Playlist](/wiki/playlist/)
- [Pintasan](/wiki/shortcuts/)
- [Format file](/wiki/file-formats/)
