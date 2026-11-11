---
source: ab2c2f14eb
---
![Halaman Umum di jendela Pengaturan, dengan tab halaman di bagian atas](/assets/img/wiki/settings-general.webp)

Jendela Pengaturan punya delapan halaman: Audio, MIDI, Umum, Tema, File, Ekstra LYNX, Proyek, dan Info. Enam yang pertama milik program. Proyek dan Info milik proyek yang terbuka dan disimpan bersamanya. Setiap halaman punya entrinya sendiri di bagian atas menu `Opsi`.

## Mengubah pengaturan

1. Buka menu `Opsi` dan pilih sebuah halaman, misalnya `Pengaturan umum...`.
2. Klik tab lain di bagian atas untuk pindah ke halaman lain.
3. Ubah pengaturannya. Berlaku seketika, kecuali bahasa dan font antarmuka, yang berlaku saat LYNX SHARE dijalankan ulang.

`F10` membuka halaman MIDI dan `F11` halaman Info.

## Audio

`Opsi › Pengaturan audio...`. Driver, perangkat, sample rate, ukuran buffer, dan perekaman. [Pengaturan audio](/wiki/audio-setup/) menjelaskan halaman ini. Di bawah `Rekaman`, `Format file` kini juga menawarkan `WAV 16 bit (laju sampel perangkat)`.

## MIDI

![Halaman MIDI: perangkat input, perangkat output dengan sinkronisasi master, dan sinkronisasi eksternal](/assets/img/wiki/settings-midi.webp)

`Opsi › Pengaturan MIDI...`

| Pengaturan | Fungsinya |
|---|---|
| Perangkat input | Setiap perangkat bisa dimatikan, mendengarkan hanya satu channel MIDI, dan memainkan channel tetap di Channel Rack, bukan yang terpilih |
| `Jenis kontroler` | Untuk tiap perangkat input: `Deteksi otomatis`, `Pengontrol generik`, atau profil kontroler, seperti Mackie Control. Baris di sampingnya menyebut port yang menerima lampu dan layar |
| `Pengontrol MPE` | Di akhir daftar channel MIDI sebuah perangkat. Tiap not datang dengan bend, timbre, dan tekanannya sendiri. Kotak kedua mengatur channel anggota |
| `Segarkan daftar perangkat` | Mencari perangkat lagi, dan membaca profil kontrolermu lagi |
| `Folder profil pengontrol` | Membuka folder untuk profil kontrolermu sendiri (`*.lynxctl`) |
| `Kirim sinkronisasi master` | Mengirim MIDI clock dengan Song Position Pointer, atau MIDI Time Code, ke perangkat output. `Offset (ms)` mengirimnya lebih lambat atau lebih awal |
| `Kirim all notes off` | Mengirim all notes off ke perangkat saat init dan setelah ekspor |
| `Sinkronisasi clock eksternal` | LYNX SHARE mengikuti MIDI clock dari input yang dipilih: tempo, start, continue, stop, dan posisi |
| `Velocity` | Untuk not dari perangkat MIDI: seperti dimainkan, lembut, keras, atau tetap |
| `Channel mode performance` | Not pada channel ini meluncurkan clip Playlist dalam mode Performance |
| `Lompatan marker song` | Not pada channel ini memindahkan lagu ke sebuah marker, bukan dimainkan |
| `Pickup (mode takeover)` | Kontroler baru menggerakkan parameter setelah mencapai nilainya saat ini |
| `Pedal kaki mengatur akhir nada` | Menyala secara bawaan: pedal sustain dan sostenuto menahan not. Jika dimatikan, pedal diteruskan ke plugin |
| `Gunakan pengontrol game` | Game controller menjalankan transport dan bisa ditautkan seperti kontroler MIDI. Mati secara bawaan |
| Tautan | Tautan kontroler proyek ini, lalu yang global, dengan `Hapus tautan` dan `Hapus semua` |

## Umum

`Opsi › Pengaturan umum...`

| Pengaturan | Fungsinya |
|---|---|
| `Gaya tata letak` | Alur kerja Pattern (bawaan), alur kerja Peluncuran klip, atau alur kerja Sederhana |
| `Template bawaan`, `Proyek saat mulai` | Template yang terbuka dengan `File › Baru`, dan apa yang terbuka saat start: `Proyek kosong`, `Template bawaan`, atau `Proyek terakhir digunakan`. `File › Simpan sebagai template...` menambahkan milikmu sendiri |
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
| `Lepaskan semua plugin baru` | Menyala: jendela plugin yang kamu tambahkan terbuka terpisah, di atas panel. Mati (bawaan): terbuka menempel di jendela utama |
| `Lukisan monofonik di piano roll` | Menyala: alat Paint mengganti not di bawah tiap not baru, dan Shift melukis di atasnya. Mati: kebalikannya |
| `Pilih modul tertaut secara otomatis` | Memberi fokus pada jendela plugin akan memilih track mixer-nya. Menyala secara bawaan |
| `Nonaktifkan prioritas keyboard ketik secara bawaan` | Untuk plugin yang kamu tambahkan sesudahnya. Mati secara bawaan |
| `Tingkat urungkan maksimum` | 25 sampai 1000 |
| `Kelola pesan` | Di bawah `Petunjuk dan peringatan`. Pesan yang dicentang ditampilkan. Hapus centang satu pesan untuk melewati pertanyaannya |
| Cadangan | Seberapa sering salinan cadangan disimpan, berapa yang disimpan per proyek, dan `Buka folder cadangan` |
| `Jalankan plugin di proses terpisah` | Plugin yang crash tidak menjatuhkan LYNX SHARE. Mati secara bawaan |
| `Ganti plugin yang hilang dengan padanan LYNX SHARE` | Menyala secara bawaan. Saat proyek memakai plugin pihak ketiga yang tidak terinstal, pengganti dari LYNX SHARE dimuat dengan pengaturan yang sesuai jika ada. Proyek tetap menyimpan plugin aslinya dan memakainya lagi setelah terinstal |
| `Batasi jumlah editor plugin yang terbuka` | Paling banyak 25 jendela editor plugin tetap terbuka. Menyala secara bawaan |
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

## Ekstra LYNX

`Opsi › Ekstra LYNX...`. Tambahan khas LYNX SHARE di satu tempat. Semuanya mati secara bawaan. Tiap sakelar adalah pengaturan yang sama dengan yang ada di menu atau halaman yang disebut barisnya.

| Pengaturan | Fungsinya |
|---|---|
| `Pengukur loudness di samping Master (LYNX)` | Loudness jangka pendek dan terintegrasi (LUFS) serta true peak Master, dengan tombol Reset |
| `Tampilkan hanya insert yang dipakai` | Menyembunyikan insert Mixer yang tidak dipakai apa pun |
| `Simpan tautan ke insert yang dihapus, berwarna abu-abu (LYNX)` | Menghapus insert mempertahankan tautan yang masuk ke dalamnya, sehingga bisa disambungkan lagi |
| `Grup Mixer mengikuti Playlist (LYNX)` | Grup Mixer mencerminkan grup track Playlist |
| `Jalur otomasi` | Otomasi bisa berada di jalur di bawah track Playlist |
| `Penggaris: birama lagu (LYNX)` | Penggaris Piano roll menampilkan nomor bar dari tempat di lagu di mana pattern itu diputar |
| `Tampilkan nilai kontrol di dekat mouse saat diarahkan ke atasnya (LYNX)` | Nilai tampil di samping kursor, bukan hanya di bilah petunjuk |
| `Setelah rekam loop, hanya putaran terakhir yang diputar` | Putaran sebelumnya tetap berada di bawahnya, dibisukan |
| `W menampilkan atau menyembunyikan jendela perangkat yang dipilih` | Sebuah tombol untuk tampilan Device |
| `Memilih perangkat akan memilih insert-nya di Mixer` | Mixer mengikuti perangkat yang dipilih di tampilan Device |
| `Gunakan pengontrol game` | Sakelar yang sama dengan yang ada di halaman MIDI |

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
- Beberapa sakelar ada di menu, bukan di jendela ini. `Latensi rendah saat merekam` ada di menu opsi rekaman di samping tombol rekam dan di menu `Perekaman ke disk` Mixer. `Gaya keyboard` ada di menu View Piano roll.
- Plugin database adalah sebuah folder, `Database plugin` di folder `LYNX` pada dokumenmu. Kamu bisa menatanya di file manager.

## Terkait

- [Pengaturan audio](/wiki/audio-setup/)
- [Controller MIDI](/wiki/midi-controllers/)
- [Pintasan](/wiki/shortcuts/)
- [Plugin pihak ketiga](/wiki/plugins/)
- [Tata letak](/wiki/layouts/)
