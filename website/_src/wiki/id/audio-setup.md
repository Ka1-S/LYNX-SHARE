---
source: 3801d5daff
---
![Halaman Audio di jendela Pengaturan, belum ada perangkat output yang dipilih](/assets/img/wiki/settings-audio.webp)

Halaman Audio di jendela Pengaturan menentukan ke mana suara keluar dan dari mana rekaman masuk. Buka lewat `Opsi › Pengaturan audio...`. Wizard penyiapan (`Opsi › Wizard penyiapan (bahasa, audio, folder plugin)...`) juga menguji output audio.

## Mengeluarkan suara

1. Buka `Opsi › Pengaturan audio...`.
2. Pilih `Driver`. Catatan di bawah kotaknya menjelaskan untuk apa driver itu cocok.
3. Pilih `Perangkat output`.
4. Klik `Putar suara uji`.
5. Baca bagian `Status`. Di situ tampil driver yang berjalan, sample rate, dan latensi output, atau `Tidak ada perangkat audio yang terbuka: pilih perangkat output.`

## Driver

Kotak `Driver` mencantumkan jenis driver di sistemmu.

| Sistem | Driver | Fungsinya |
|---|---|---|
| Windows | Windows Audio (bersama) | Program lain tetap bersuara. Windows mencampur semua program dalam langkah tetap, jadi buffer tidak bisa lebih kecil dari langkah itu |
| Windows | Windows Audio (bersama, periode latensi rendah) | Tetap bersama, tetapi meminta Windows memakai periode pencampuran yang lebih pendek. Hanya sebagian driver yang mendukungnya |
| Windows | Windows Audio (eksklusif, latensi terendah) | LYNX SHARE memakai perangkat sendirian. Program lain jadi senyap selama LYNX SHARE memutar |
| Windows | DirectSound (lama) | Jalur audio Windows lama dengan latensi tinggi. Dipakai kalau yang lain gagal |
| Windows | ASIO | Driver latensi rendah. Driver ASIO dari pabrikan bekerja dengan perangkatnya sendiri |
| Windows | LYNX ASIO (bawaan) | Driver ASIO milik LYNX SHARE, untuk perangkat apa pun, pemutaran dan perekaman. `Pengaturan driver...` memilih mode bersama atau eksklusif dan perangkatnya |
| macOS | Core Audio | Audio bawaan sistem, dipakai bersama program lain |
| Linux | PipeWire (native) | LYNX SHARE berbicara langsung dengan PipeWire. `System default` mengikuti output yang dipilih di pengaturan suara sistem |
| Linux | JACK / PipeWire | Untuk server JACK yang sedang berjalan, atau PipeWire dengan `pipewire-jack` |
| Linux | ALSA | Berbicara langsung dengan kartu suara. `default` atau `pipewire` berbagi perangkat, sedangkan perangkat `hw:` mengambil kartunya untuk LYNX SHARE sendiri |

Di Linux, saat pertama kali dijalankan, LYNX SHARE membuka yang pertama dari PipeWire, JACK dan ALSA yang punya perangkat. Pilihan yang tersimpan dipertahankan.

## Pengaturan di halaman ini

| Pengaturan | Fungsinya |
|---|---|
| `Driver` | Jenis driver, dari tabel di atas |
| `Perangkat output` | Tempat Master diputar |
| `Perangkat input` | Sumber untuk rekaman audio dan untuk input insert Mixer. Bawaannya `(tidak ada)` |
| `Sample rate` | Rate yang ditawarkan perangkat |
| `Ukuran buffer` | Ukuran yang ditawarkan perangkat, masing-masing dengan latensinya dalam milidetik. Buffer kecil merespons lebih cepat saat kamu bermain. Buffer besar memberi CPU lebih banyak ruang, dengan lebih sedikit bunyi klik |
| `Pengaturan driver...` | Membuka jendela milik driver itu sendiri (driver ASIO) |
| `Putar suara uji` | Memutar suara uji lewat perangkat output |
| `Rekam audio input` | Merekam dua channel input pertama ke insert baru kalau tidak ada insert yang disiagakan |
| `Format file` | Rekaman berupa WAV 24-bit atau WAV 32-bit float, dengan sample rate perangkat |
| `Ukur round trip` | Memutar 8 klik dari `Output ping` ke `Input ping` lewat kabel atau loopback, lalu mengukur jedanya supaya take jatuh di tempat kamu memainkannya |
| `Penyesuaian halus (sample)` | Menggeser setiap take baru. Nilai positif berarti lebih awal di Playlist |
| `Trek metronom` | Track Mixer tempat metronom diputar. `---` berarti Master. Klik tidak pernah ikut dalam ekspor |
| `Putar not terpotong saat transport` | Memainkan not yang melewati posisi awal |
| `Reset plugin saat transport` | Menghapus ekor efek saat pemutaran dimulai atau melompat |

## Kalau tidak ada suara

- Lihat `Status` dulu. Kalau tidak ada perangkat yang terbuka, pilih perangkat output.
- Coba driver lain. Di Windows, `Windows Audio (bersama)` membuat program lain tetap bersuara, dan `DirectSound (lama)` ada untuk saat yang lain gagal.
- Periksa fader Master di [Mixer](/wiki/mixer/) dan knob `MASTER` di toolbar.
- Dalam mode Pattern hanya pattern saat ini yang diputar. Kalau kosong, kamu tidak mendengar apa-apa. Tekan `L` untuk mode Song.
- Di Linux, kalau suara putus-putus, `Status` menunjukkan apakah thread audio mendapat prioritas real-time. Solusinya ada di [Pemecahan masalah](/wiki/troubleshooting/).

## Baik diketahui

- ASIO hanya ada di Windows.
- Dengan `Windows Audio (bersama)`, buffer terkecil adalah langkah pencampuran sistem. Catatan di bawah kotak `Driver` menyebut angkanya untuk perangkatmu. Untuk latensi lebih rendah, pakai mode eksklusif atau driver ASIO.
- Dengan PipeWire, buffer berkisar dari 32 sampai 2048 sample dan sample rate dari 44,1 sampai 192 kHz. Kalau PipeWire mempertahankan rate-nya sendiri, ia me-resample stream LYNX SHARE.
- Dengan PipeWire, saat perangkat bernama dicabut, LYNX SHARE kembali ke `System default` dan mempertahankan ukuran buffer.
- `Status` juga menampilkan beban CPU audio dan menghitung dropout (buffer underrun).
- Kamu bisa menampilkan daftar driver dan perangkat tanpa membuka jendelanya. Lihat `--devices` di [Command line](/wiki/command-line/).

## Terkait

- [Pengaturan](/wiki/settings/)
- [Merekam](/wiki/recording/)
- [Controller MIDI](/wiki/midi-controllers/)
- [Pemecahan masalah](/wiki/troubleshooting/)
