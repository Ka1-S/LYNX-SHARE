---
source: 7ca5e689db
---
![Palet tambah cepat: instrumen di kiri, efek di kanan, masing-masing diberi label bawaan atau engine FL bawaan](/assets/img/wiki/quickadd.webp)

LYNX SHARE membawa instrumen dan efeknya sendiri, jadi proyek bisa diputar tanpa plugin pihak ketiga. Ada tiga jenis: karya asli LYNX SHARE (synth, sampler, pemutar SoundFont, drum, EQ, compressor, limiter, reverb, delay), engine yang ditulis untuk memainkan pengaturan plugin bawaan FL Studio, dan efek yang menggantikan plugin pihak ketiga yang terkenal.

Masing-masing punya halamannya sendiri di [katalog](/instruments-and-effects/). `Bantuan › Plugin bawaan (daftar online)...` membuka daftar beserta status setiap engine.

## Menambahkan instrumen dan efek

1. Tekan `F6` untuk menampilkan Channel Rack.
2. Klik kotak `+ tambah channel` di bawah baris terakhir dan pilih instrumen. Menu mengelompokkannya per kategori.
3. Tekan `F9` untuk menampilkan Mixer dan pilih insert tempat channel itu dimainkan.
4. Klik slot kosong di panel efek dan pilih efek. Jendelanya terbuka.
5. Klik nama slot untuk menyembunyikan atau menampilkan jendela itu lagi.

## Pemilih plugin

Tekan `F8` untuk membuka pemilih plugin. Ia menutupi layar dan menampilkan setiap generator dan efek sebagai gambar editornya sendiri, dengan kategori sebagai tab di sepanjang bagian bawah.

| Perintah | Tombol | Hasilnya |
|---|---|---|
| Cari | Ketik | Meredupkan plugin yang tidak cocok |
| Pindah antar plugin | `Up` / `Down` | Menggeser sorotan |
| Tambah | `Enter`, klik dua kali, atau klik kanan | Generator menjadi channel baru. Efek masuk ke insert Mixer yang dipilih |
| Seret gambar | | Ke Channel Rack untuk channel baru, ke sebuah channel untuk mengganti instrumennya, atau ke insert atau slot Mixer |
| Tutup | `Esc` atau `F8` | Menutup pemilih tanpa menambahkan apa pun |

Gambar plugin milik LYNX SHARE sendiri digambar oleh LYNX SHARE di latar belakang. Plugin pihak ketiga menampilkan kotak polos dengan nama pembuatnya sampai editornya dibuka satu kali.

`Edit › Palet tambah cepat...` membuka daftar polos sebagai gantinya: instrumen di kiri, efek di kanan, disaring saat kamu mengetik. `Tab` berpindah antar daftar dan `Enter` menambahkan item yang disorot. Database plugin di [Browser](/wiki/browser/) (`Shift+F8`) mencantumkan plugin yang sama sebagai pohon.

![Database plugin di Browser dengan folder Engine FL terbuka](/assets/img/wiki/browser-plugins.webp)

## Preset

Jendela efek bawaan punya bilah preset di headernya.

| Kontrol | Fungsinya |
|---|---|
| Panah | Preset sebelumnya dan berikutnya |
| Nama preset | Membuka menu: `Preset LYNX SHARE`, `Preset Anda`, `Preset FL Studio` jika ada, `Simpan preset...`, dan `Tampilkan folder preset` |
| `A` / `B` | Dua pengaturan untuk dibandingkan. Tombol di sebelahnya menyalin pengaturan yang sedang kamu dengar ke yang lain |

Nama preset, beserta pengaturan A dan B, disimpan bersama proyek.

## Arti "membuka pengaturan FL"

Plugin bawaan FL Studio tidak pernah dimuat. LYNX SHARE punya engine sendiri untuk masing-masing, dan engine itu membaca pengaturan yang disimpan plugin FL. Ini berlaku di dua tempat:

- Proyek `.flp`. Setiap generator dan efek FL terbuka pada engine LYNX SHARE-nya dengan pengaturan yang tersimpan, dan kontrolnya tetap bisa diedit dan diotomasi.
- Preset `.fst`. Klik dua kali preset synth di Browser untuk mendapatkan channel baru, atau seret preset efek ke insert Mixer. Klik kanan slot efek yang terisi untuk `Preset FL`, yang mencantumkan preset yang ditemukan di instalasi FL Studio kamu.

Seberapa dekat setiap engine dengan aslinya dinyatakan per engine, dari Exact sampai Placeholder. Tingkatannya dijelaskan di [Pindahan dari FL Studio](/wiki/coming-from-fl-studio/).

## Engine yang butuh instalasi FL Studio kamu

LYNX SHARE tidak berisi kode, audio, atau data gelombang milik Image-Line. Beberapa engine membutuhkan wave, sample, impuls, atau tabel milik plugin aslinya, dan membacanya dari instalasi FL Studio kamu sendiri selagi berjalan.

| Jenis | Engine |
|---|---|
| Instrumen | FL Keys, DirectWave, BassDrum, Toxic Biohazard, FL Slayer, MiniSynth, Kepler, Transistor Bass, Sawer, Autogun, Fruity DrumSynth Live, BooBass, Harmor, Harmless, GMS, Morphine |
| Efek | Hardcore, Maximus, Soundgoodizer, Fruity Convolver, Distructor, Vocodex |

Jika file tidak ditemukan, penambahan engine semacam itu bisa gagal dengan pesan bahwa file data instalasi FL Studio tidak ditemukan atau tidak bisa dibaca.

## Pengganti plugin pihak ketiga

Saat proyek memakai efek pihak ketiga yang tidak terinstal, efek LYNX SHARE dengan peran yang sama mengambil alih slotnya: reverb untuk reverb, delay untuk delay.

- Slot menampilkan nama plugin aslinya dengan lencana LYNX SHARE.
- Jika state yang tersimpan bisa dibaca, pengaturannya diterjemahkan. Jika tidak, pengganti dimulai dari preset default.
- Plugin asli beserta pengaturannya tetap ada di proyek. Setelah plugin itu terinstal, proyek terbuka lagi dengannya.
- Klik kanan slot pengganti dan pilih `Bypass semua pengganti LYNX SHARE di proyek ini` untuk mendengar proyek tanpa mereka.

Sakelarnya adalah `Ganti plugin yang hilang dengan padanan LYNX SHARE` di `Opsi › Pengaturan umum...`, aktif secara default. Di proyek baru, efek yang sama ada di menu slot kosong di bawah `Padanan LYNX SHARE`.

## Perlu diketahui

- Instrumen yang hilang belum diganti. Plugin instrumen yang hilang menjadi synth bawaan yang dipilih berdasarkan nama channel.
- Otomasi parameter plugin asli tetap disimpan tetapi tidak menggerakkan pengganti.
- Paket FLEX dienkripsi dan tidak pernah dibuka. Channel FLEX mendapat patch synth yang dipilih berdasarkan nama presetnya.
- Satu insert punya 10 slot efek.

## Terkait

- [Katalog instrumen dan efek](/instruments-and-effects/)
- [Plugin pihak ketiga](/wiki/plugins/)
- [Channel Rack](/wiki/channel-rack/)
- [Mixer](/wiki/mixer/)
- [Pindahan dari FL Studio](/wiki/coming-from-fl-studio/)
