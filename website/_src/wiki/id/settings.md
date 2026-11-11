---
source: 841fdd7d67
---
![Halaman Umum di jendela Pengaturan, dengan tujuh tab halaman di bagian atas](/assets/img/wiki/settings-general.webp)

Jendela Pengaturan punya tujuh halaman: Audio, MIDI, Umum, Tema, File, Proyek, dan Info. Lima yang pertama milik program. Proyek dan Info milik proyek yang terbuka dan disimpan bersamanya. Setiap halaman punya entrinya sendiri di bagian atas menu `Opsi`.

## Mengubah pengaturan

1. Buka menu `Opsi` dan pilih sebuah halaman, misalnya `Pengaturan umum...`.
2. Klik tab lain di bagian atas untuk pindah ke halaman lain.
3. Ubah pengaturannya. Berlaku seketika, kecuali bahasa dan font antarmuka, yang berlaku saat LYNX SHARE dijalankan ulang.

`F10` membuka halaman MIDI dan `F11` halaman Info.

## Audio

`Opsi › Pengaturan audio...`. Driver, perangkat, sample rate, ukuran buffer, dan perekaman. [Pengaturan audio](/wiki/audio-setup/) menjelaskan halaman ini.

## MIDI

![Halaman MIDI: perangkat input, perangkat output dengan sinkronisasi master, dan sinkronisasi eksternal](/assets/img/wiki/settings-midi.webp)

`Opsi › Pengaturan MIDI...`

| Pengaturan | Fungsinya |
|---|---|
| Perangkat input | Setiap perangkat bisa dimatikan, mendengarkan hanya satu channel MIDI, dan memainkan channel tetap di Channel Rack, bukan yang terpilih |
| `Segarkan daftar perangkat` | Mencari perangkat lagi |
| `Kirim sinkronisasi master` | Mengirim MIDI clock dengan Song Position Pointer, atau MIDI Time Code, ke perangkat output. `Offset (ms)` mengirimnya lebih lambat atau lebih awal |
| `Kirim all notes off` | Mengirim all notes off ke perangkat saat init dan setelah ekspor |
| `Sinkronisasi clock eksternal` | LYNX SHARE mengikuti MIDI clock dari input yang dipilih: tempo, start, continue, stop, dan posisi |
| `Velocity` | Untuk not dari perangkat MIDI: seperti dimainkan, lembut, keras, atau tetap |
| `Channel mode performance` | Not pada channel ini meluncurkan clip Playlist dalam mode Performance |
| `Lompatan marker song` | Not pada channel ini memindahkan lagu ke sebuah marker, bukan dimainkan |
| `Pickup (mode takeover)` | Kontroler baru menggerakkan parameter setelah mencapai nilainya saat ini |
| Tautan | Tautan kontroler proyek ini, lalu yang global, dengan `Hapus tautan` dan `Hapus semua` |

## Umum

`Opsi › Pengaturan umum...`

| Pengaturan | Fungsinya |
|---|---|
| `Gaya tata letak` | Alur kerja Pattern (bawaan) atau alur kerja peluncuran klip |
| `Klip sesi dari set Live` | Tempat clip sesi dari set Live yang diimpor: kisi clip, atau Playlist setelah aransemen |
| `Bahasa`, `Font antarmuka` | Berlaku pada start berikutnya |
| `Ukuran antarmuka`, `Kepadatan menu` | Skala seluruh antarmuka, dan menu yang ringkas atau lapang |
| `Bentuk gelombang` | Bersih (warna clip) atau warna spektral, opsional dengan channel stereo terpisah |
| `Nama not`, `Tuts hitam` | Nama Inggris, Jerman, atau solfegio, dengan kres atau mol |
| `Gaya plugin` | Tampilan jendela plugin bawaan: otomatis, atau satu gaya untuk setiap plugin |
| `Animasi`, `Laju animasi` | Animasi antarmuka, dan seberapa sering playhead dan meter bergerak |
| `Gambar dengan GPU (Direct2D)` | Matikan kalau driver grafis crash atau layar berkedip |
| Saat memulai | Splash screen, suara pembuka, dan mencari plugin baru di latar belakang pada setiap start |
| Pengeditan | Keyboard ketik ke piano, membiarkan editor channel tetap terbuka, mengingat waktu seek, me-loop clip pattern yang di-stretch |
| `Tingkat urungkan maksimum` | 25 sampai 1000 |
| Cadangan | Seberapa sering salinan cadangan disimpan, berapa yang disimpan per proyek, dan `Buka folder cadangan` |
| `Jalankan plugin di proses terpisah` | Plugin yang crash tidak menjatuhkan LYNX SHARE. Mati secara bawaan |
| `Ganti plugin yang hilang dengan padanan LYNX SHARE` | Aktif secara bawaan. Proyek tetap menyimpan plugin aslinya |
| `Laporan crash` | Tanya setelah crash atau freeze (bawaan), selalu kirim, atau jangan pernah kirim |
| `Periksa pembaruan secara otomatis` | Pemeriksaan pembaruan saat start, dengan `Periksa sekarang` |

## Tema

`Opsi › Tema (warna, latar belakang)...`

| Pengaturan | Fungsinya |
|---|---|
| Kotak tema | Memilih preset. `Impor...` dan `Ekspor...` membaca dan menulis file `.lynxtheme`, `Simpan preset...` menyimpan tema saat ini, `Bawaan` meresetnya |
| `Warna aksen`, `Latar belakang`, `Ruang kerja` | Tiga warna sebuah tema. Warna ruang kerja bisa mengikuti latar belakang secara otomatis |
| Gambar ruang kerja | PNG, JPEG, atau GIF di belakang jendela, dengan `Penyesuaian gambar`, `Opasitas gambar`, dan `Redupkan` |
| Suara pembuka | Suaramu sendiri sepanjang 30 detik atau kurang, atau suara bawaan LYNX SHARE |

## File

`Opsi › Pengaturan file (folder pencarian)...`

| Pengaturan | Fungsinya |
|---|---|
| Folder pencarian untuk sample yang hilang | Jendela File hilang mencari sendiri di folder ini dan subfoldernya |
| Folder Browser | Ditampilkan di daftar folder Browser, dan juga dicari untuk sample yang hilang |
| Folder plugin | Dipindai untuk plugin oleh `Opsi › Pindai plugin` |

## Proyek

![Halaman Proyek: pengaturan waktu, groove dan tuning, timebase, dan opsi Playlist](/assets/img/wiki/settings-project.webp)

`Opsi › Pengaturan proyek (tempo, birama)...`

| Pengaturan | Fungsinya |
|---|---|
| `Pengaturan waktu` | Pembagian waktu (beat per bar, step per beat) atau birama tradisional (pembilang dan penyebut) |
| `Tempo`, `Beat per bar` | Tempo proyek dan jumlah beat dalam satu bar |
| `Swing`, `Master pitch` | Jumlah swing global dan master pitch dalam sen |
| `Crossfade otomatis` | Clip audio yang bertumpuk pada satu track di-crossfade sepanjang tumpukannya |
| `Crossfade comp` | Panjang crossfade yang dipakai saat comping take, dalam milidetik |
| `Mainkan not terpotong di klip` | Not yang mulai sebelum clip pattern dimainkan dari awal clip |
| `Declick cepat untuk grup potong` | Ramp lebih cepat saat grup potong memotong not |
| `Hukum panning` | Sirkular (tengah di 0 dB, +3 dB di sisi tujuan panning) atau Segitiga (sisi yang lebih keras mempertahankan levelnya). Untuk insert Mixer dan channel Sampler |

## Info

`Opsi › Info proyek...`. Judul, pembuat, genre, tautan web, dan komentar proyek, serta `Tampilkan halaman ini saat proyek dibuka`. Bagian bawah menampilkan kapan proyek dibuat dan lama pengerjaannya.

## Baik diketahui

- Timebase dikunci pada 960 tick per not seperempat. Proyek dari FL Studio dikonversi ke sana.
- Perubahan birama sepanjang lagu tidak ada di halaman Proyek. Tambahkan marker birama di penggaris Playlist.
- `Jalankan plugin di proses terpisah` berlaku untuk plugin yang dibuka setelahnya. Buka ulang proyek untuk memindahkan yang lain.
- Pengaturan pembaruan tidak ada di versi Microsoft Store, karena Store yang memperbaruinya.
- Pintasan ada di `Opsi › Kelola pintasan...`, bukan di halaman Pengaturan.

## Terkait

- [Pengaturan audio](/wiki/audio-setup/)
- [Controller MIDI](/wiki/midi-controllers/)
- [Pintasan](/wiki/shortcuts/)
- [Plugin pihak ketiga](/wiki/plugins/)
- [Tata letak](/wiki/layouts/)
