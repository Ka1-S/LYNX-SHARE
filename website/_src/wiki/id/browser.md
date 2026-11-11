---
source: 8df354b49e
---
![Browser di sisi kiri dengan tiga tab, kotak pencarian, pohon folder, dan strip pratinjau](/assets/img/wiki/browser-tree.webp)

Browser menampilkan folder sample kamu, isi proyek saat ini, dan plugin yang dikenal LYNX SHARE. Tekan `Alt+F8` untuk menampilkan atau menyembunyikannya. `F8` saja membuka pemilih plugin, tampilan layar penuh terpisah berisi semua instrumen dan efek.

## Memuat sample ke proyek

1. Tekan `Alt+F8` untuk menampilkan Browser.
2. Di tab `SEMUA`, klik sebuah folder untuk membukanya.
3. Klik file audio untuk mendengarnya. `Up` dan `Down` berpindah antar file dan memainkan pratinjau masing-masing.
4. Klik dua kali file itu untuk mendapatkan channel baru berisi sample tersebut di [Channel Rack](/wiki/channel-rack/).
5. Atau seret file ke [Playlist](/wiki/playlist/) untuk mendapatkan clip audio.

## Tab

| Tab | Isi |
|---|---|
| SEMUA | Proyek saat ini, Database plugin, dan semua folder sample |
| PROYEK | Hanya yang dipakai proyek saat ini: pattern, efek, generator, otomasi, file audio |
| BERBINTANG | File dan folder yang kamu beri bintang (klik kanan, lalu `Berbintang`) |

## Menelusuri dan mempratinjau

- Klik folder untuk membuka atau menutupnya. Klik kanan untuk membuka menu (ciutkan semua, tampilkan di pengelola file).
- Klik file audio untuk mendengarnya. Strip pratinjau di bawah daftar menampilkan bentuk gelombangnya. Klik strip itu untuk mendengar file sekali lagi, atau seret dari strip itu seolah-olah kamu menyeret filenya.
- Panel di atas strip pratinjau menampilkan tanggal, ukuran, dan lokasi file yang dipilih.

## Memakai sample

| Aksi | Hasilnya |
|---|---|
| Klik dua kali | Channel baru berisi sample |
| Seret ke Channel Rack | Channel baru, atau lepaskan di sebuah baris untuk memuatnya di sana |
| Seret ke Playlist | Clip audio |
| `Enter` | Memuat file ke channel yang dipilih |
| `Shift+Up` / `Shift+Down` | Pindah ke file sebelumnya atau berikutnya dan memuatnya ke channel yang dipilih |

Yang terakhir memungkinkan kamu mendengarkan berbagai sample langsung di tempat selagi pattern diputar.

## Jenis file lain

- **Proyek**: klik dua kali untuk membukanya.
- **File MIDI**: klik dua kali untuk membukanya sebagai proyek.
- **SoundFont atau SFZ**: klik untuk mempratinjau, klik dua kali untuk membuat channel LYNX Soundfont (atau memuatnya ke channel yang dipilih).
- **Preset Mixer**: klik dua kali untuk memuatnya ke insert Mixer yang dipilih, atau seret ke sebuah insert.
- **Preset efek**: seret ke insert Mixer.

## Database plugin

Entri Database plugin mencantumkan instrumen dan efek bawaan LYNX SHARE serta plugin yang terinstal. `Shift+F8` langsung menuju ke sana.

- Klik dua kali sebuah instrumen untuk menambahkannya sebagai channel baru, atau seret ke Channel Rack.
- Klik dua kali sebuah efek untuk menambahkannya ke insert [Mixer](/wiki/mixer/) yang dipilih, atau seret ke sebuah insert atau slot efek.
- `F2` mengganti nama plugin yang dipilih.
- Klik kanan sebuah plugin dan pilih `Pindahkan ke folder` untuk menyortir plugin ke dalam folder. Menu slot Mixer dan menu tambah Channel Rack lalu mencantumkan setiap folder teratas.

## Tab PROYEK

Pattern yang tercantum di sini bisa diklik untuk menjadikannya pattern saat ini, diklik dua kali untuk membukanya di [Piano Roll](/wiki/piano-roll/), atau diseret ke Playlist. Bagian proyek saat ini juga mencantumkan riwayat urungkan: klik sebuah langkah untuk kembali ke sana.

## Pencarian

Tekan `Ctrl+F` untuk menaruh kursor di kotak pencarian. Pencarian mencakup folder yang dipilih (atau semua folder) beserta isi di bawahnya. Di tab PROYEK, pencarian mencakup item proyek.

Semua kata harus cocok. Kotak ini juga menerima `OR`, tanda minus untuk mengecualikan kata, dan wildcard `*`:

```
kick -loop
snare OR clap
hat*.wav
```

Folder diindeks di latar belakang. `Segarkan indeks pencarian` di menu Browser membangun ulang indeks.

## Folder milikmu sendiri

Pakai `Tambah folder` di daftar folder Browser untuk menambahkan folder berisi sample, proyek, atau SoundFont. Folder pencarian ada di `Opsi › Pengaturan file (folder pencarian)`.

## Tombol

Tombol panah, `Enter`, dan `F2` hanya bekerja jika Browser sedang fokus.

| Perintah | Tombol | Hasilnya |
|---|---|---|
| Tampilkan Browser | `Alt+F8` | Menampilkan atau menyembunyikan Browser |
| Pemilih plugin | `F8` | Membuka pemilih plugin layar penuh |
| Database plugin | `Shift+F8` | Langsung ke entri Database plugin |
| Cari | `Ctrl+F` | Menaruh kursor di kotak pencarian |
| Pindah dan pratinjau | `Up` / `Down` | Memilih file sebelumnya atau berikutnya dan memutarnya |
| Tutup folder | `Left` | Menutup folder atau naik ke folder induknya |
| Buka folder | `Right` | Membuka folder |
| Muat | `Enter` | Memuat file ke channel yang dipilih |
| Pindah dan muat | `Shift+Up` / `Shift+Down` | Pindah ke file sebelumnya atau berikutnya dan memuatnya ke channel yang dipilih |
| Ganti nama plugin | `F2` | Mengganti nama plugin yang dipilih di Database plugin |

## Perlu diketahui

- Menghapus folder dari daftar tidak menghapusnya dari disk.
- `F8` dan `Alt+F8` berbeda. `F8` adalah pemilih plugin, `Alt+F8` adalah Browser.
- Hasil pencarian berasal dari indeks yang dibangun di latar belakang. `Segarkan indeks pencarian` di menu Browser membangunnya ulang.
- `Enter` memuat file ke channel yang dipilih. Klik dua kali membuat channel baru.

## Terkait

- [Channel Rack](/wiki/channel-rack/)
- [Plugin pihak ketiga](/wiki/plugins/)
- [Format file](/wiki/file-formats/)
- [Urungkan dan riwayat](/wiki/undo-history/)
- [Instrumen dan efek](/instruments-and-effects/)
