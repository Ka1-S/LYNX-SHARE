---
source: de4ead4a7a
---
Halaman ini menelusuri seluruh jalurnya sekali jalan: pattern drum, melodi, aransemen, mix, dan file hasil ekspor. Setiap bagian menaut ke halaman yang membahas jendelanya secara lengkap. Jika di titik mana pun kamu tidak mendengar apa-apa, lihat [Pengaturan audio](/wiki/audio-setup/).

## Membuat pattern drum

1. Pilih `File › Baru dari template › Kosong`. Proyek dimulai dengan lima channel, Kick, Clap, Hat, Snare, dan Synth, tanpa step.
2. Tekan `F6` jika [Channel Rack](/wiki/channel-rack/) belum tampil.
3. Klik beberapa step di baris Kick. Menyeret menggambar beberapa sekaligus, dan klik kanan menghapus.
4. Tekan `Space`. Transport dalam mode Pattern, jadi pattern diputar dalam loop. Tekan `Space` lagi untuk berhenti.
5. Beri baris Clap, Hat, dan Snare step masing-masing.
6. Tekan `F2` untuk memberi nama dan warna pada pattern.

Untuk menambah suara yang belum ada, klik kotak `+ tambah channel` di bawah baris terakhir dan pilih instrumen dari menu. Untuk memakai sample milikmu sendiri, buka [Browser](/wiki/browser/) dengan `Alt+F8`, klik file audio untuk mendengarnya, lalu klik dua kali untuk menambahkannya sebagai channel baru.

## Menulis melodi

1. Tekan `F4`. LYNX SHARE pindah ke pattern kosong berikutnya dan meminta namanya.
2. Klik kanan nama channel Synth dan pilih `Piano Roll`.
3. Dengan Pensil (`P`), klik di grid untuk menambahkan not.
4. Seret not untuk memindahkannya dan seret ujungnya untuk mengubah panjangnya. Klik kanan menghapus not.
5. Tekan `Space` untuk mendengar pattern selagi kamu mengedit.

`Ctrl+T` mengubah keyboard ketik menjadi piano, jadi kamu bisa mencoba not di `Z` sampai `M` dan `Q` sampai `P` lebih dulu. Matikan lagi sebelum memakai pintasan huruf seperti `P`.

## Menyusun aransemen pattern

1. Tekan `F5` untuk menampilkan [Playlist](/wiki/playlist/).
2. Di panel Pemilih di sisi kiri Playlist, klik pattern drum. Sekarang itulah sumber clip.
3. Dengan Pensil (`P`), klik di sebuah track untuk menaruh pattern. Klik lagi lebih ke kanan untuk menaruhnya sekali lagi, atau pakai Kuas (`B`) dan seret untuk melukis clip yang bersambung ujung ke ujung.
4. Klik pattern melodi di Pemilih dan taruh di track lain.
5. Tekan `L` untuk beralih ke mode Song, lalu `Space`. Playlist diputar dari posisi song. Klik penggaris untuk mengaturnya.

## Mix dan menambahkan efek

1. Tekan `F9` untuk menampilkan [Mixer](/wiki/mixer/). Lima channel awal sudah dimainkan lewat insert 1 sampai 5, dan channel yang ditambahkan dari menu `+ tambah channel` mengambil insert kosong. Angka di kotak rute channel di Channel Rack menunjukkan insert yang mana.
2. Untuk memindahkan channel ke insert kosong miliknya sendiri, pilih channel di Channel Rack dan tekan `Ctrl+L`. Setiap insert memakai nama dan warna channel.
3. Seret fader insert untuk mengatur levelnya.
4. Klik sebuah insert untuk memilihnya. Panel efek di sebelah kanan menampilkan kesepuluh slotnya.
5. Klik slot kosong dan pilih efek. Klik nama slot untuk menampilkan atau menyembunyikan jendela efek. Lampu slot mem-bypass efek.

## Mengekspor song

![Jendela ekspor: rentang dan tail di kiri, format, bit depth, dan sample rate di kanan, opsi mix di bawah](/assets/img/wiki/export.webp)

1. Tekan `Ctrl+S` dan simpan proyek lebih dulu.
2. Pilih `File › Ekspor › File Wave...`. Untuk format lain, menu yang sama punya `File MP3...`, `File OGG...`, dan `File FLAC...`.
3. Atur `Rentang` ke `Song (Playlist)`. Pilihan lainnya adalah `Pattern saat ini` dan `Pilihan Playlist`.
4. Biarkan opsi lainnya apa adanya.
5. Klik `Ekspor...`, pilih nama file, dan tunggu render selesai.

## Fungsi tiap tombol

| Perintah | Tombol | Hasilnya |
|---|---|---|
| Channel Rack | `F6` | Menampilkan atau menyembunyikan Channel Rack |
| Pattern kosong berikutnya | `F4` | Pindah ke pattern kosong berikutnya dan menamainya |
| Piano Roll | `F7` | Menampilkan atau menyembunyikan Piano Roll |
| Playlist | `F5` | Menampilkan atau menyembunyikan Playlist |
| Mode Pattern atau Song | `L` | Mengganti apa yang diputar oleh `Space` |
| Mixer | `F9` | Menampilkan atau menyembunyikan Mixer |
| Kirim ke insert kosong | `Ctrl+L` | Memberi channel terpilih insert Mixer miliknya sendiri |
| Simpan | `Ctrl+S` | Menyimpan proyek sebagai `.lynx` |
| Ekspor | `Ctrl+R` | Membuka jendela ekspor dengan format yang terakhir dipakai |

## Perlu diketahui

- Dalam mode Pattern kamu hanya mendengar pattern saat ini. Jika song senyap setelah disusun, periksa apakah transport dalam mode Song.
- Jendela ekspor dimulai pada `Song (Playlist)` ketika transport dalam mode Song dan Playlist punya clip. Jika tidak, dimulai pada `Pattern saat ini`.
- Klik metronom tidak pernah ikut dalam ekspor.
- Bagian bawah jendela ekspor menampilkan panjang song dalam bar, total waktunya, dan ruang disk yang dibutuhkan file.
- Secara default proyek baru punya Limiter di insert Master. Itu bagian dari apa yang kamu dengar dan yang kamu ekspor.
- Urungkan adalah `Ctrl+Z`. Ditekan lagi, ia mengulangi langkah itu. `Ctrl+Alt+Z` mundur lebih jauh.

## Terkait

- [Tur jendela](/wiki/tour/)
- [Ekspor](/wiki/exporting/)
- [Otomasi](/wiki/automation/)
- [Merekam](/wiki/recording/)
- [Instrumen dan efek](/instruments-and-effects/)
