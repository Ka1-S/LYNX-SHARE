---
source: a06504f3a0
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
| `Channel MIDI mana pun` | Mendengarkan semua channel MIDI, atau hanya satu channel. `Pengontrol MPE` ada di akhir daftar ini |
| `Memainkan channel terpilih` | Not masuk ke channel yang dipilih di Channel Rack, atau selalu ke satu channel tetap |
| `Velocity` | `Seperti dimainkan`, `Lembut (permainan ringan terdengar lebih keras)`, `Keras (perlu permainan lebih keras)`, atau `Tetap (setiap not dengan velocity penuh)` |
| `Channel mode performance` | Not pada channel MIDI ini meluncurkan clip Playlist selama mode Performance aktif |
| `Lompatan marker song` | Not pada channel MIDI ini memindahkan lagu ke sebuah marker, bukan dimainkan |
| `Jenis kontroler` | `Deteksi otomatis`, `Pengontrol generik`, atau sebuah profil kontroler |
| `Pedal kaki mengatur akhir nada` | Menyala secara bawaan. Pedal sustain (CC 64) atau pedal sostenuto (CC 66) menahan not. Jika dimatikan, pedal diteruskan ke plugin sebagai kontroler |

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

Halaman MIDI mencantumkan tautan di bawah `Tautan (proyek ini, lalu global)`. Perangkat yang `Jenis kontroler`-nya `Pengontrol generik` hanya melakukan apa yang kamu tautkan sendiri.

Parameter plugin pihak ketiga dijangkau lewat `Otomasikan parameter plugin` di menu channel atau menu slot efek. Setiap parameter di sana punya item tautan yang sama. Lihat [Otomasi](/wiki/automation/).

## Profil kontroler

Profil kontroler mengatur tombol transport, fader, knob, pad, lampu, dan layar sebuah perangkat, tanpa ada yang perlu ditautkan.

1. Buka `Opsi › Pengaturan MIDI...` (`F10`) dan centang perangkat di bawah `Perangkat input`.
2. Biarkan `Jenis kontroler`-nya di `Deteksi otomatis`. Nama dalam tanda kurung adalah profil yang ditemukan dari nama port perangkat.
3. Jika yang tampil `Pengontrol generik`, pilih sendiri profilnya dari daftar.
4. Baca baris di samping kotaknya. `Feedback to` menyebut port output yang menerima lampu, motor fader, dan teks layar.

| Profil | Yang dipetakan |
|---|---|
| `Mackie Control Universal` | Fader ke volume insert dan Master. V-Pot ke pan, ke parameter plugin channel terpilih, atau ke volume Channel rack. Arm, solo, mute, dan select per strip, bank fader, play, stop, record, rewind, forward, mode Pattern atau Song, metronom, undo, dan jog wheel. Layar menampilkan nama dan nilai |
| `Mackie Control Extender (XT)` | Hal yang sama untuk 8 strip tambahan. Unit-unit berbagi satu bank, sesuai urutan daftar `Perangkat input` |
| `Novation Launchkey MK3 (DAW mode)` | Pot ke pan, volume, atau parameter plugin, fader ke volume, tombol fader memilih sebuah insert. Tombol bank, play, stop, record, loop, metronom, dan undo. Pad Session adalah 16 step channel terpilih. Pakai di port kedua keyboard |
| `Akai APC Key 25 mk2` | Knob ke volume, pan, atau parameter plugin. Dua baris pad teratas adalah 16 step channel terpilih dan tiga baris di bawahnya memainkan not. Tombol track memilih sebuah insert. Stop, play, dan record. Pakai di port kedua |
| `Arturia KeyLab Essential mk3 (Mackie Control port)` | Pemetaan Mackie Control, di port MCU/HUI milik keyboard |
| `Korg nanoKONTROL2 (Mackie Control mode)` | Pemetaan Mackie Control. Ia tidak terdeteksi: jalankan unit dalam mode Mackie Control dan pilih tipe ini secara manual |

Knob dan fader yang mengirim posisi memakai soft takeover: mereka baru menggerakkan kontrol setelah mencapai nilainya. Kontrol yang kamu tautkan sendiri tetap dengan tautannya. `Folder profil pengontrol` membuka folder untuk profilmu sendiri (`*.lynxctl`), dan `Segarkan daftar perangkat` membacanya lagi.

Profil bawaan ditulis dari dokumentasi masing-masing pembuat. Profil-profil itu belum dicoba di perangkat kerasnya.

## Kontroler MPE

MPE masih di tahap pertama. Kontroler MPE memainkan tiap not dengan pitch bend, timbre (CC 74), dan tekanannya sendiri.

1. Di halaman MIDI, buka kotak perangkat yang berbunyi `Channel MIDI mana pun` dan pilih `Pengontrol MPE`.
2. Di kotak yang muncul di sampingnya, pilih channel anggota, dari `Kanal 2` sampai `Channels 2-16`.
3. Mainkan. Untuk merekam, lihat [Rekaman](/wiki/recording/).

- Not yang direkam menyimpan satu nilai dari masing-masing: bend-nya sebagai fine pitch, timbre-nya sebagai Mod X, dan tekanan terkuatnya sebagai Mod Y. Bukan berupa kurva.
- Dari instrumen bawaan, Sampler dan synth bawaan mengikuti nilai-nilai ini.
- Plugin pihak ketiga menerima MPE saat `MPE` menyala di bawah `MIDI` pada `Pengaturan wrapper` jendela plugin. Tiap not lalu keluar di channel MIDI-nya sendiri, 2 sampai 16.
- Belum dicoba dengan kontroler MPE sungguhan.

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
- Not yang ditahan pedal direkam sampai saat pedal dilepas.
- Clip otomasi tidak mengambil kembali kontrol saat kamu menggerakkannya dari kontroler. Kontrol mengikuti otomasinya lagi saat pemutaran dimulai berikutnya.

## Terkait

- [Merekam](/wiki/recording/)
- [Otomasi](/wiki/automation/)
- [Channel Rack](/wiki/channel-rack/)
- [Pengaturan](/wiki/settings/)
- [Pintasan](/wiki/shortcuts/)
