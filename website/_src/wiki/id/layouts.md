---
source: 1c353ba51c
---
![Alur kerja peluncuran klip: kisi clip dan scene di kiri, Picker dan Playlist di kanan](/assets/img/cliplaunch.webp)

`Tampilan › Gaya tata letak` punya dua entri di 0.1.25: `Alur kerja pattern (bawaan)` dan `Alur kerja peluncuran klip`. Pilihan yang sama ada di `Opsi › Pengaturan umum`, di bawah Workflow. Pilihan itu tersimpan saat kamu menutup program.

Alur kerja Pattern adalah Channel Rack, pattern, dan Playlist. Alur kerja peluncuran klip menambahkan kisi clip dengan scene di samping Playlist. Pattern dan Channel Rack tetap tersedia.

## Meluncurkan clip di kisi

1. Pilih `Tampilan › Gaya tata letak › Alur kerja peluncuran klip`. Kisi clip muncul di samping Playlist.
2. Klik kanan slot kosong, pilih `Taruh pattern di sini`, lalu pilih sebuah pattern. Klik dua kali slot kosong menaruh pattern saat ini di sana.
3. Klik segitiga di kiri clip untuk meluncurkannya. Kalau transport berhenti, transport dimulai.
4. Klik segitiga sebuah scene, di kolom `Scene`, untuk meluncurkan setiap clip di baris itu.
5. Klik `Hentikan semua klip` untuk menghentikan semuanya.
6. Klik `Kembali ke Playlist` supaya track memutar clip Playlist-nya lagi.

## Yang berubah di layar

- Kisi clip ditampilkan di samping Playlist. Track Playlist menjadi kolom dan scene menjadi baris.
- Bilah judul Playlist mendapat sakelar `Keduanya`, `Grid`, `Song`. `Grid` hanya menampilkan kisi, `Song` hanya Playlist.
- Pada pertama kali, Channel Rack disembunyikan. `F6` atau tombolnya di toolbar menampilkannya.
- Jalur otomasi Playlist dinyalakan.
- Tata letak jendela disimpan per gaya, jadi setiap gaya kembali seperti yang kamu tinggalkan.

## Kisi

Clip yang diluncurkan tidak langsung mulai. Clip menunggu titik kuantisasi peluncuran berikutnya, dan segitiganya berkedip sampai saat itu. Kotak di bagian bawah kisi mengatur kuantisasi peluncuran global, yang dipakai setiap clip yang diatur ke `Global`. Penghentian menunggu dengan cara yang sama.

Klik kanan sebuah clip untuk melihat pengaturannya.

| Pengaturan | Fungsinya |
|---|---|
| `Kuantisasi peluncuran` | Kapan clip ini mulai: `Global`, `Tidak ada`, `8 bar` sampai `1/32` |
| `Mode peluncuran` | `Pemicu`, `Gate`, `Alih`, atau `Ulang` |
| `Legato (pertahankan posisi)` | Clip mengambil alih posisi putar dari clip yang sebelumnya diputar di track itu |
| `Loop` | Aktif: clip berulang. Mati: diputar sekali, lalu track berhenti |
| `Aksi lanjutan` | Yang terjadi setelah clip: dua aksi dengan peluang masing-masing, dan waktu sampai keduanya dijalankan |
| `Delete` | Mengosongkan slot |

Klik kanan slot kosong untuk `Tombol berhenti (adegan menghentikan trek ini di sini)`. Mengklik kotak di kiri slot kosong menghentikan track itu. Klik kanan sebuah scene untuk `Sisipkan adegan`, `Tambah adegan di bawah`, `Hapus adegan`, `Ganti nama adegan...`, dan tempo scene.

Menu slot juga punya dua opsi untuk seluruh kisi: `Kembalikan trek ke Playlist saat transport berhenti` dan `Aksi lanjutan di seluruh kisi`.

| Perintah | Tombol | Fungsinya |
|---|---|---|
| Pindah | Tombol panah | Memilih slot lain |
| Meluncurkan clip | `Enter` | Meluncurkan clip yang dipilih |
| Meluncurkan scene | `Ctrl+Enter` | Meluncurkan scene yang dipilih |
| Menghapus clip | `Del` | Mengosongkan slot yang dipilih |

## Set Ableton Live

Saat kamu membuka set Live, clip Session dan scene-nya masuk ke kisi clip, beserta pengaturan peluncurannya. `File › Ekspor › Set Ableton Live (.als)` menulis kisi itu kembali sebagai clip Session. `Opsi › Pengaturan umum` punya `Klip sesi dari set Live`, tempat `Playlist, setelah aransemen` menaruhnya di Playlist sebagai gantinya.

## Kembali

Pilih `Tampilan › Gaya tata letak › Alur kerja pattern (bawaan)`. Tata letak jendela yang kamu punya di alur kerja Pattern kembali, dan jalur otomasi kembali ke pengaturan sebelumnya. Clip di kisi tetap ada di proyek.

## Baik diketahui

- Track yang meluncurkan clip tetap senyap di Playlist sampai kamu menekan `Kembali ke Playlist`. Opsi `Kembalikan trek ke Playlist saat transport berhenti` melakukannya untukmu di setiap penghentian.
- Di 0.1.25 sebuah slot diisi dari menunya atau dengan klik dua kali. Kamu tidak bisa menyeret clip atau file ke dalam slot.
- Follow action ada untuk clip, bukan untuk scene.
- Jalur otomasi yang dinyalakan manual selama berada di alur kerja peluncuran klip tidak dipertahankan saat kamu kembali.
- Dengan alur kerja Pattern terpilih, tidak ada bagian kisi yang dimuat.

## Terkait

- [Playlist](/wiki/playlist/)
- [Pindahan dari Ableton Live](/wiki/coming-from-ableton/)
- [Mengimpor proyek](/wiki/importing-projects/)
- [Yang hanya dilakukan LYNX: alur kerja](/why/#workflows)

Tata letak pemula yang disederhanakan belum ada di 0.1.25. Lihat [Tata letak sederhana](/wiki/simple-layout/).
