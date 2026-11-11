---
source: f08a3da66d
---
![Channel Rack dengan baris-baris step](/assets/img/rack.webp)

Channel Rack mencantumkan setiap instrumen (channel) dalam proyek, satu per baris, dan menampilkan step dari pattern saat ini. Tekan `F6` untuk menampilkan atau menyembunyikannya. Klik kanan tombol Pattern di transport juga menampilkannya.

## Membuat pattern drum

1. Tekan `F6` untuk menampilkan Channel Rack.
2. Klik kotak `+ tambah channel` di bawah baris terakhir dan pilih instrumen dari menu. Kamu juga bisa mengklik dua kali sebuah sample di [Browser](/wiki/browser/) untuk mendapatkan channel sampler baru.
3. Klik step di baris channel untuk mengaktifkannya. Seret untuk menggambar deretan step.
4. Klik kanan sebuah step untuk menghapusnya.
5. Tekan `Space` untuk memutar. Dalam mode Pattern, pattern diulang dalam loop.
6. Seret knob volume baris itu untuk mengatur levelnya.

## Memprogram step

- Klik sebuah step untuk mengaktifkannya. Menyeret menggambar deretan step. Jika seretan dimulai dari step yang menyala, yang terjadi malah penghapusan.
- Klik kanan atau seret dengan klik kanan untuk menghapus.
- Mengklik setelah akhir pattern menambahkan step dan memperpanjang pattern.
- Baris yang notenya bukan step biasa (tuts lain, not lebih panjang, slide) menampilkan miniatur not. Mengkliknya membuka not itu di [Piano Roll](/wiki/piano-roll/).
- Kamu bisa menjatuhkan file audio ke rack untuk membuat channel.

Menu channel (klik kanan nama channel) berisi `Isi setiap 2 step`, `Isi setiap 4 step`, `Isi setiap 8 step`, `Isian lanjutan`, serta `Putar ke kiri` dan `Putar ke kanan`.

## Jalur grafik

![Jalur grafik terbuka di bawah channel Kick, dengan satu batang velocity per step](/assets/img/wiki/rack-graph.webp)

`Ctrl+K` menampilkan jalur grafik di bawah channel yang dipilih. Tombol di sisi kirinya memilih apa yang diedit: Velocity, Pan, Release, Pitch halus, Mod X, atau Mod Y. Menyeret mengatur nilai per step, seret dengan klik kanan menggambar rampa, dan `Alt`+klik mengatur ulang sebuah step.

## Bagian-bagian sebuah baris

| Bagian | Fungsinya |
|---|---|
| Lampu mute | Klik untuk mute. `Ctrl`+klik atau klik kanan untuk solo. |
| Knob pan dan volume | Seret atau pakai roda mouse. |
| Kotak rute | Seret atau putar roda untuk memilih insert Mixer. Klik dua kali untuk menampilkan insert itu di Mixer. Klik kanan mengambil insert kosong berikutnya. |
| Tombol nama | Klik membuka atau menutup editor channel. `Alt`+klik membiarkan editor lain tetap terbuka. `Ctrl`+klik untuk mendengarkan. Seret ke atas atau bawah untuk mengubah urutan. Klik kanan membuka menu channel. |
| Pemilih | Klik memilih satu channel. `Ctrl`+klik atau klik kanan menambah atau mencabut pilihan. `Shift`+klik memilih satu rentang. Klik dua kali memilih semua. |

## Pattern

Rack selalu mengedit pattern saat ini. Pemilih pattern di rack menampilkan namanya: klik nama itu untuk melihat daftar, atau pakai tanda panah. `Shift`+roda mouse di atas judul rack juga mengganti pattern.

Kotak panjang pattern di rack mengatur panjangnya dalam bar (seret atau pakai roda mouse). Klik dua kali untuk panjang otomatis yang diambil dari not.

Knob swing di toolbar utama berlaku untuk semua channel. `Mix swing` di menu channel mengatur seberapa jauh satu channel mengikutinya.

| Perintah | Tombol | Hasilnya |
|---|---|---|
| Pattern berikutnya | `Numpad +` | Menjadikan pattern berikutnya sebagai pattern saat ini |
| Pattern sebelumnya | `Numpad -` | Menjadikan pattern sebelumnya sebagai pattern saat ini |
| Pattern kosong berikutnya | `F4` | Pindah ke pattern kosong berikutnya dan meminta nama |
| Ganti nama dan warna | `F2` | Mengganti nama dan mewarnai pattern saat ini |

## Memainkan not dari keyboard PC

`Ctrl+T` mengubah keyboard ketik menjadi piano: `Z` sampai `M` dan `Q` sampai `P` memainkan channel yang dipilih. Selagi aktif, pintasan satu huruf milik editor dijeda.

## Tombol

Ini bekerja saat Channel Rack sedang fokus.

| Perintah | Tombol | Hasilnya |
|---|---|---|
| Pilih channel | `Up` / `Down` | Memilih channel di atas atau di bawah |
| Perluas pilihan | `Shift+Up` / `Shift+Down` | Menambahkan channel di atas atau di bawah ke pilihan |
| Pindahkan channel | `Alt+Up` / `Alt+Down` | Menggeser channel terpilih ke atas atau ke bawah |
| Pilih semua | `Ctrl+A` | Memilih semua channel |
| Potong, salin, tempel | `Ctrl+X` / `Ctrl+C` / `Ctrl+V` | Memotong, menyalin, atau menempel step milik channel terpilih |
| Putar step | `Ctrl+Shift+Left` / `Ctrl+Shift+Right` | Memutar step milik channel terpilih |
| Kloning | `Alt+C` | Mengkloning channel terpilih |
| Hapus | `Alt+Del` | Menghapus channel terpilih |
| Grup | `Alt+G` | Memasukkan channel terpilih ke grup filter baru |
| Rutekan ke Mixer | `Ctrl+L` | Mengirim channel terpilih ke insert Mixer yang kosong |
| Mute | `1` sampai `0` | Me-mute atau membatalkan mute channel 1 sampai 10 |
| Solo | `Ctrl+1` sampai `Ctrl+0` | Solo channel 1 sampai 10 |
| Jalur grafik | `Ctrl+K` | Menampilkan atau menyembunyikan jalur grafik |

## Hal lain di menu channel

`Piano Roll`, `Ganti nama dan warna`, `Ganti` (menukar instrumen), `Kloning`, `Delete`, `Arpeggiator`, `Titik loop`, `Tulis MIDI ke`, `Urutkan channel`, `Pilih channel yang tidak dipakai`, dan `Pengaturan channel`.

## Perlu diketahui

- Hanya kotak `+ tambah channel` yang membuka menu tambah.
- Rack tidak punya pattern sendiri. Mengganti pattern saat ini di mana pun mengubah apa yang ditampilkan rack.
- Baris yang menampilkan miniatur not tidak bisa diedit sebagai step. Pakai Piano Roll untuk itu.
- Tombol angka hanya menjangkau sepuluh channel pertama.
- Tombol di tabel membutuhkan fokus pada rack. Klik rack lebih dulu.

## Terkait

- [Piano Roll](/wiki/piano-roll/)
- [Playlist](/wiki/playlist/)
- [Mixer](/wiki/mixer/)
- [Browser](/wiki/browser/)
- [Pintasan](/wiki/shortcuts/)
