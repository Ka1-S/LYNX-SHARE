---
source: edf15b371e
---
![Jendela utama dengan proyek demo: toolbar di atas, Browser di kiri, Playlist di belakang, dan Channel Rack mengambang di atasnya](/assets/img/wiki/desktop.webp)

LYNX SHARE punya satu jendela utama. Toolbar membentang di sepanjang bagian atas. Di bawahnya ada desktop yang memuat lima jendela: Browser, Playlist, Channel Rack, Piano Roll, dan Mixer. Pada awal pertama, Browser ada di kiri, Playlist memenuhi sisanya, dan Channel Rack mengambang di atasnya. Piano Roll dan Mixer tersembunyi sampai kamu memanggilnya.

## Menemukan jalan

1. Arahkan mouse ke kontrol mana pun. Bilah petunjuk di bawah menu menjelaskan fungsinya dan tombol mouse serta tombol keyboard yang dipakainya.
2. Tekan `F6` untuk menyembunyikan Channel Rack, dan `F6` lagi untuk menampilkannya.
3. Tekan `F9` untuk menampilkan Mixer, lalu `F7` untuk Piano Roll.
4. Seret jendela pada strip judulnya untuk memindahkannya. Klik dua kali judul untuk memaksimalkannya, dan sekali lagi untuk memulihkannya.
5. Pilih `Tampilan › Reset tata letak` untuk mengembalikan semuanya.

## Toolbar

![Toolbar: menu dan bilah petunjuk, transport, knob master, opsi rekaman, snap dan pattern, tombol jendela, tombol file, tampilan chord, meter, dan monitor output](/assets/img/wiki/toolbar-1920.webp)

Dari kiri ke kanan, dengan tata letak default:

| Grup | Isinya |
|---|---|
| Menu dan bilah petunjuk | Menu `File`, `Edit`, `Tambah`, `Pattern`, `Tampilan`, `Opsi`, `Alat`, dan `Bantuan`, dengan bilah petunjuk di bawahnya. Selalu yang pertama |
| Transport | Tombol mode `Pattern` dan `Song`, Putar, Stop, dan Rekam, tempo dalam BPM, dan posisi sebagai bar, beat, dan step |
| Volume dan pitch master | Knob volume `MASTER` dan knob `PITCH`, dalam sen |
| Opsi rekaman | Metronom, hitungan awal, rekam berulang, input per langkah, dan sakelar rekaman lainnya |
| Snap dan pattern | Snap utama (`Garis (mengikuti zoom)` secara default), pattern saat ini, tombol untuk pattern baru, dan menu pattern |
| Pintasan jendela | Satu tombol per jendela, dan pemilih plugin |
| File dan urungkan | Baru, Buka, Simpan, Ekspor, Urungkan, dan Ulangi |
| Deteksi akor | Menyebutkan nama chord yang sedang dimainkan di perangkat MIDI atau keyboard ketik |
| CPU, memori, dan polifoni | Beban CPU audio, memori yang terpakai, dan jumlah voice |
| Monitor output | Osiloskop output. Satu klik berpindah ke mode lainnya |

Ada juga grup bernama `Multilink ke kontroler` yang tersembunyi secara default.

`Opsi › Toolbar` mengubah toolbar: `Edit toolbar (seret panel untuk memindahkan, menambah atau menghapusnya)`, `Pemisah`, `Sembunyikan panel toolbar`, `Preset`, sakelar untuk tiap grup, dan `Ukuran ikon` (kecil, sedang, atau besar).

## Transport

| Kontrol | Tombol | Fungsinya |
|---|---|---|
| Mode Pattern | `L` | Memutar pattern saat ini dalam loop. Klik kanan tombolnya untuk menampilkan Channel Rack |
| Mode Song | `L` | Memutar aransemen Playlist. Klik kanan tombolnya untuk menampilkan Playlist |
| Putar | `Space` | Memutar. Ditekan lagi, ia berhenti dan kembali ke tempat mulai. `Ctrl+Space` menjeda |
| Stop | | Berhenti dan kembali ke awal. Klik dua kali juga mengirim semua not off |
| Rekam | `R` | Rekaman aktif atau mati. Lihat [Merekam](/wiki/recording/) |

## Bilah petunjuk

Saat diam, bilah petunjuk berbunyi `Point at anything for help`, dengan `F1` untuk daftar pintasan, `F5` sampai `F9` untuk jendela, dan `Space` untuk putar dan stop. Di atas sebuah kontrol, ia menyebutkan nama kontrol itu dan mencantumkan apa yang dilakukan seret, `Ctrl`, `Alt`, dan klik kanan di sana. Pekerjaan panjang seperti pemindaian plugin di latar belakang melaporkan progresnya di sana.

## Lima jendela

| Jendela | Tombol | Kegunaannya |
|---|---|---|
| [Playlist](/wiki/playlist/) | `F5` | Menyusun clip pattern, audio, dan otomasi menjadi song |
| [Channel Rack](/wiki/channel-rack/) | `F6` | Satu baris per instrumen, dengan step dari pattern saat ini |
| [Piano Roll](/wiki/piano-roll/) | `F7` | Not milik satu channel di pattern saat ini |
| [Mixer](/wiki/mixer/) | `F9` | Insert dengan fader, efek, dan rute |
| [Browser](/wiki/browser/) | `Alt+F8` | Sample, preset, isi proyek, dan database plugin |

Setiap tombol menampilkan jendela dan memberinya fokus, atau menyembunyikannya jika sudah punya fokus. `F8` saja membuka pemilih plugin, yang bukan salah satu dari kelima jendela. Tombol jendela di toolbar melakukan hal yang sama dengan tombol keyboard. Klik kanan salah satunya menampilkan jendela itu dalam keadaan maksimal. `Enter` memaksimalkan atau memulihkan Playlist.

## Cara jendela bergerak dan dipasang

Setiap jendela punya strip judul. Di kirinya ada tombol menu dengan opsi milik jendela itu sendiri, lalu `Terlepas (jendela sendiri)`, `Dimaksimalkan`, dan `Tutup`. Di kanannya ada tombol maksimalkan dan tutup.

- Seret strip judul untuk memindahkan jendela. Ia menempel ke jendela lain dan ke tepi. Tahan `Alt` untuk memindahkan tanpa menempel.
- Seret tepi untuk mengubah ukuran.
- `Terlepas (jendela sendiri)` mengubah jendela menjadi jendela sistem tersendiri, untuk monitor kedua. Jendela yang terlepas punya tombol `Pasang` yang mengembalikannya. `Tampilan › Panel terlepas (jendela sendiri)` mencantumkan kelimanya.
- `Tampilan › Kembalikan panel di luar layar` memulihkan jendela yang keluar dari desktop.
- `Tampilan › Simpan tata letak sebagai...` menyimpan susunan dengan sebuah nama, dan `Tampilan › Muat tata letak` mengembalikannya.

Browser adalah latar belakang desktop: ia tetap terpasang di sisi dan jendela lain selalu berada di atasnya.

## Perlu diketahui

- Sebelum kamu memindahkan, mengubah ukuran, atau melepas sesuatu, jendela kembali ke tempat default-nya setiap kali ukuran jendela utama berubah. Setelah itu, susunanmu diingat dan ikut menyesuaikan dengan jendela.
- `F12` menutup jendela plugin, pengaturan channel, dan alat. Ia tidak menutup kelima jendela utama.
- Selagi piano keyboard ketik aktif (`Ctrl+T`), tombol huruf memainkan not dan sebagian besar pintasan satu huruf dijeda.
- `Tampilan › Gaya tata letak` berpindah antara alur kerja pattern (bawaan) dan alur kerja peluncuran klip, yang menambahkan kisi clip di samping Playlist.

## Terkait

- [Lagu pertamamu](/wiki/first-song/)
- [Tata letak](/wiki/layouts/)
- [Pintasan](/wiki/shortcuts/)
- [Pengaturan](/wiki/settings/)
