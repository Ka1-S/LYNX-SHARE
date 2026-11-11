---
source: 3b54a39c97
---
![Halaman MIDI di pengaturan: perangkat input, perangkat output dengan sinkronisasi master, dan sinkronisasi eksternal](/assets/img/wiki/settings-midi.webp)

LYNX SHARE menerima not dan gerakan kontroler dari perangkat input MIDI mana pun. Perangkatnya diatur di halaman MIDI pada pengaturan: `Opsi › Pengaturan MIDI...`, atau `F10`. Tanpa hardware, keyboard ketik dan keyboard sentuh juga memainkan not.

## Menghubungkan keyboard

1. Colokkan keyboard dan buka `Opsi › Pengaturan MIDI...` (`F10`).
2. Cari di bawah `Perangkat input` lalu centang. Kalau tidak tercantum, klik `Segarkan daftar perangkat`.
3. Biarkan dua kotak pada `Channel MIDI mana pun` dan `Memainkan channel terpilih`.
4. Tutup pengaturan dan pilih sebuah channel di [Channel Rack](/wiki/channel-rack/).
5. Bermainlah. Channel yang dipilih berbunyi.

## Perangkat input

| Pengaturan | Fungsinya |
|---|---|
| Kotak centang | Menyalakan atau mematikan perangkat |
| `Channel MIDI mana pun` | Mendengarkan semua channel MIDI, atau hanya satu channel |
| `Memainkan channel terpilih` | Not masuk ke channel yang dipilih di Channel Rack, atau selalu ke satu channel tetap |
| `Velocity` | `Seperti dimainkan`, `Lembut (permainan ringan terdengar lebih keras)`, `Keras (perlu permainan lebih keras)`, atau `Tetap (setiap not dengan velocity penuh)` |
| `Channel mode performance` | Not pada channel MIDI ini meluncurkan clip Playlist selama mode Performance aktif |
| `Lompatan marker song` | Not pada channel MIDI ini memindahkan lagu ke sebuah marker, bukan dimainkan |

## Menautkan knob ke sebuah kontrol

1. Klik kanan knob atau fader di LYNX SHARE.
2. Pilih `Tautkan ke kontroler MIDI (gerakkan knob pada kontroler)`.
3. Gerakkan knob atau fader di hardware-mu. Tautan terbentuk.

Tautan ini disimpan bersama proyek. Untuk menghapusnya, klik kanan kontrolnya lagi dan pilih `Lepas tautan kontroler`. Item menu itu menampilkan nomor CC yang dipakai.

| Perintah | Tombol | Fungsinya |
|---|---|---|
| `Override global link...` | tidak ada | Membuat tautan yang berlaku di setiap proyek. Disimpan di pengaturan, bukan di proyek |
| `Hapus tautan global` | tidak ada | Menghapusnya lagi |
| `Multilink ke kontroler` | `Ctrl+J` | Selama aktif, kontrol yang kamu gerakkan ditautkan, berurutan, ke kontrol hardware yang kamu gerakkan berikutnya. Ada di menu `Opsi` |
| `Pickup (mode takeover)` | tidak ada | Ada di halaman MIDI. Kontroler baru menggerakkan parameter setelah mencapai nilainya saat ini, jadi tidak ada yang melompat |

Halaman MIDI mencantumkan tautan di bawah `Tautan (proyek ini, lalu global)`. `Jenis kontroler` di halaman yang sama berbunyi `Kontroler generik`.

Parameter plugin pihak ketiga dijangkau lewat `Otomasikan parameter plugin` di menu channel atau menu slot efek. Setiap parameter di sana punya item tautan yang sama. Lihat [Otomasi](/wiki/automation/).

## Keyboard ketik

`Ctrl+T`, atau `Opsi › Keyboard ketik ke piano`, mengubah keyboard komputer menjadi piano. Deretan dari `Z` sampai `M` dan dari `Q` sampai `P` memainkan channel yang dipilih.

- Selama aktif, tombol huruf dan angka menjadi not. Tombol alat berhuruf tunggal di Piano Roll dan Playlist hanya berfungsi saat keyboard ketik mati.
- Selalu dimainkan dengan velocity Piano Roll. Pengaturan `Velocity` di halaman MIDI untuk perangkat MIDI.
- Sakelar yang sama ada di `Opsi › Pengaturan umum...`.

## Keyboard sentuh

![Keyboard sentuh: tombol oktaf, ukuran tuts, mode velocity, dan deretan tuts piano mulai dari C4](/assets/img/wiki/touch-keyboard.webp)

`Tampilan › Keyboard sentuh` (`Alt+F7`) membuka piano yang dimainkan dengan mouse atau layar sentuh. Piano ini memainkan channel yang dipilih di Channel Rack, dan menampilkan namanya di sebelah kanan.

| Pengaturan | Fungsinya |
|---|---|
| `-` dan `+` | Satu oktaf turun atau naik |
| Ukuran tuts | `Tuts kecil`, `Tuts sedang`, atau `Tuts besar` |
| `Velocity: posisi sentuhan` | Dekat bagian atas tuts berbunyi lembut, dekat bagian bawah keras |
| `Velocity: tetap` | Setiap not dengan velocity yang diatur lewat slider |
| `Nama not` | Menampilkan nama pada tuts |

## Sinkronisasi MIDI

`Perangkat output` di halaman MIDI mengirim transport ke hardware dan software lain. Centang `Kirim sinkronisasi master` untuk sebuah perangkat dan pilih `MIDI clock` (dengan Start, Continue, Stop, dan Song Position Pointer) atau MIDI Time Code. `Offset (ms)` mengirim sinkronisasi lebih lambat dengan nilai positif dan lebih awal dengan nilai negatif. `Kirim all notes off` mengirim pesan itu ke perangkat saat init dan setelah ekspor.

`Sinkronisasi clock eksternal` bekerja sebaliknya: LYNX SHARE mengikuti MIDI clock dari input yang kamu pilih, beserta tempo, Start, Continue, Stop, dan Song Position Pointer-nya. `Offset sinkronisasi eksternal (milidetik)` membuat LYNX SHARE berbunyi lebih lambat dengan nilai positif dan lebih awal dengan nilai negatif. `Status` menunjukkan apakah ada clock yang berjalan.

## Baik diketahui

- Memilih input untuk `Sinkronisasi clock eksternal` menyalakan perangkat itu di bawah `Perangkat input`.
- Tempo yang digerakkan oleh kontroler MIDI yang tertaut tidak mengubah ukuran clip audio. Ketik atau atur tempo akhirnya.
- Untuk merekam apa yang kamu mainkan, lihat [Merekam](/wiki/recording/). Not yang masuk bisa di-snap ke tangga nada di [Piano Roll](/wiki/piano-roll/).
- MIDI CC, channel aftertouch, dan pitch bend yang dikirim ke plugin bisa diotomasi dengan `Otomasikan kontroler MIDI (CC, aftertouch, pitch bend)`.

## Terkait

- [Merekam](/wiki/recording/)
- [Otomasi](/wiki/automation/)
- [Channel Rack](/wiki/channel-rack/)
- [Pengaturan](/wiki/settings/)
- [Pintasan](/wiki/shortcuts/)
