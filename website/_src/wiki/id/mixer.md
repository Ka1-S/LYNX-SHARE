---
source: b6e9ef0dd0
---
![Mixer](/assets/img/mixer.webp)

Setiap channel diputar lewat sebuah insert Mixer. Setiap insert punya fader, pan, mute dan solo, meter puncak, sepuluh slot efek dengan jumlah dry/wet masing-masing, dan EQ bawaan. Semua insert berakhir di Master kecuali kamu mengarahkannya ke tempat lain. Tekan `F9` untuk menampilkan atau menyembunyikan Mixer.

Satu proyek bisa punya hingga 127 insert. Mixer punya strip tambah untuk insert baru. Kotak di bilah judul Mixer memilih satu dari delapan tata letak strip, dari `Ringkas` sampai `Ekstra besar`.

## Memasang efek pada channel

1. Di [Channel Rack](/wiki/channel-rack/), pilih channel lalu tekan `Ctrl+L`. Channel masuk ke insert kosong, yang mengambil nama dan warnanya.
2. Tekan `F9` untuk menampilkan Mixer lalu klik strip insert itu.
3. Di panel efek, klik slot kosong dan pilih sebuah efek.
4. Klik nama slot untuk menampilkan atau menyembunyikan jendela efeknya.
5. Klik lampu slot untuk mem-bypass efek dan membandingkan.

## Mengirim channel ke insert

Di Channel Rack, seret kotak rute channel (atau pakai roda di atasnya) untuk memilih nomor insert. Klik dua kali kotak itu menampilkan insert tersebut di Mixer.

`Ctrl+L` di Channel Rack bekerja pada beberapa channel terpilih sekaligus. Masing-masing masuk ke insert kosongnya sendiri.

Saat Mixer sedang fokus, `Ctrl+L` menautkan channel terpilih ke insert terpilih, dan `Ctrl+Shift+L` menautkannya ke insert berurutan mulai dari situ.

## Memilih dan memindahkan insert

- Klik sebuah strip untuk memilihnya.
- `Ctrl`+klik, atau seret melintasi strip, memilih sebuah rentang. `Ctrl+Shift`+klik menambah atau menghapus satu.
- Seret strip ke samping, atau tekan `Alt+Left` dan `Alt+Right`, untuk memindahkan insert terpilih.
- Klik kanan strip untuk menu insert. `F2` mengganti nama dan warna insert terpilih.
- `Del`, atau `Delete` di menu insert, menghapus insert terpilih. LYNX SHARE bertanya dulu. Centang `Ingat pilihan saya` untuk melewati pertanyaan itu lain kali.

Memindahkan, menyisipkan, atau menghapus insert membuat send, sidechain, dan otomasi tetap menempel pada insert-nya.

Bagian `Mixer` di menu insert dan `Opsi › Mixer` punya dua perintah lagi. `Atur insert seperti Playlist` memindahkan insert yang tertaut ke track Playlist ke depan, sesuai urutan Playlist. Dengan `Simpan tautan ke insert yang dihapus, berwarna abu-abu (LYNX)` menyala, send yang masuk ke insert yang dihapus tetap ada, berwarna abu-abu, jadi kamu bisa menyambungkannya lagi.

## Mute dan solo

Klik lampu mute untuk me-mute insert. Klik kanan atau `Ctrl`+klik untuk solo. `S` men-solo insert terpilih, dan `Alt+S` men-solo-nya bersama insert yang mengumpannya.

Insert yang dibisukan tetap menggerakkan meternya. Di proyek baru dan di proyek FL Studio yang diimpor, ia juga tetap menjalankan efeknya, dan hanya outputnya ke Master serta send-nya yang diputus. Supaya insert yang dibisukan berhenti memproses, nyalakan `Insert yang dibisukan tidak memproses (LYNX, hemat CPU; proyek ini)` di bagian `Mixer` pada menu insert atau di `Opsi › Mixer`. Meternya lalu berhenti dan efeknya mulai dari awal saat kamu membatalkan bisunya.

## Efek

Panel efek menampilkan sepuluh slot dari insert terpilih.

- Seret sebuah slot, atau pakai roda di atasnya, untuk memindahkannya ke atas atau ke bawah.
- Klik kanan sebuah slot untuk opsinya (ganti, pindah, hapus).
- Selama menu sebuah slot terbuka, `Del` menghapus plugin di slot itu.
- `Kelompokkan ke rak efek audio` di menu slot memasukkan efek ke rack baru, dan `Kelompokkan semua ke rak efek audio` melakukannya untuk seluruh chain. `Pisahkan grup` mengembalikan efek rack satu-chain ke slot-slot.
- Sakelar di header panel menyalakan atau mematikan semua efek insert itu.
- Kamu bisa menyeret efek dari database Plugin di [Browser](/wiki/browser/) ke sebuah insert atau slot efek.
- `Suara bersih` di menu insert menaruh rantai pembersih vokal siap pakai pada insert.

## Edison di sebuah slot

Edison, editor dan perekam audio, bisa berada di slot efek mana pun, beberapa per insert. Jendelanya adalah editor audio dengan satu baris kontrol rekaman.

- `Buka editor audio` (`Ctrl+E`) di menu insert memuat Edison ke slot kosong pertama insert dan membukanya. `Buka editor audio dan rekam` (`Shift+E`) memuatnya siap merekam saat lagu diputar.
- Mode rekam adalah `Sekarang`, `Saat input` (bawaan: ia menunggu input melewati threshold), `Input`, dan `Saat putar`. `Tambahkan` menaruh rekaman baru setelah audio yang sudah ada. Waktu rekam maksimum adalah `Selamanya` atau 1, 3, 5, 10, 30, atau 60 menit: slot baru dimulai pada 30, `Buka editor audio dan rekam` mengaturnya ke 5.
- `Sync` memutar audio Edison pada waktu lagu selama lagu diputar.
- Edison terus merekam setelah kamu menutup jendelanya.
- `Alat › Makro › Hapus semua instance Edison` mengosongkan setiap slot Edison di proyek, setelah ada pertanyaan.
- Slot Edison di proyek FL Studio (.flp) terbuka bersama audionya.

## EQ insert

Setiap insert punya EQ sendiri. Seret sebuah band untuk mengatur frekuensi dan levelnya, pakai roda untuk lebarnya, dan klik dua kali sebuah band untuk mengembalikannya ke 0 dB. Klik kanan judul EQ untuk mem-bypass-nya. Kalau kamu ingin Mixer tanpa EQ bawaan, proyek baru bisa diatur untuk dimulai dengan mixer bersih.

## Routing, send, dan sidechain

![Mixer dengan insert Kick terpilih, send-nya digambar sebagai kabel di bawah strip dan panel efek di kanan](/assets/img/wiki/mixer-sends.webp)

Pilih insert yang ingin kamu arahkan. Setiap strip lain lalu punya sakelar rute:

- Klik sakelar di strip lain untuk mengirim insert terpilih ke sana. Menyeret setelah mengaktifkan mengatur level send.
- `Ctrl`+klik mengarahkan ke insert itu saja.
- `Shift`+klik membuat send sidechain. `Ctrl+Shift`+klik membuatnya khusus sidechain.
- Klik kanan membuka pilihan yang sama sebagai menu.
- Untuk send pre-fader, klik kanan sakelar rute atau knob level send lalu centang `Send pra-fader (fader trek ini tidak mengubahnya)`. Send itu lalu ditandai `PRE`.

Efek dengan input samping, seperti compressor, punya pemilih `Sidechain dari` yang mencantumkan insert yang bisa didengarkan. Memilih salah satunya membuatkan rutenya.

## Grup dan fader VCA

Grup Mixer dilipat dengan chevron-nya (atau `Alt`+klik untuk semua grup). Fader VCA menggerakkan level insert anggotanya selagi mereka tetap punya fader sendiri. Buat dari menu insert: di bawah Group, pilih `VCA › VCA baru dari yang dipilih`.

## Merekam

Setiap insert punya tombol siaga rekam. Dengan insert yang disiagakan, Rec dan Play merekam input audionya ke clip Playlist. `Shift+R` menyiagakan insert terpilih dan memulai rekaman dalam satu langkah. Lihat [Merekam](/wiki/recording/).

Saat kamu memilih input eksternal untuk insert yang juga merekam audio lain, LYNX SHARE bertanya apakah akan mengalihkannya ke `Hanya input eksternal`, sehingga take hanya berisi input itu. `Mixer › Perekaman ke disk › Kompensasi latensi` di menu insert menyala secara bawaan. Jika dimatikan, take yang direkam ditempatkan tanpa kompensasi latensi sama sekali.

## Tombol

`Ctrl+L` dan `Ctrl+Shift+L` berarti seperti ini hanya saat Mixer sedang fokus.

| Perintah | Tombol | Fungsinya |
|---|---|---|
| Menautkan channel | `Ctrl+L` | Menautkan channel terpilih ke insert terpilih |
| Menautkan ke insert berurutan | `Ctrl+Shift+L` | Menautkan channel terpilih ke insert mulai dari yang terpilih |
| Memindahkan insert | `Alt+Left` / `Alt+Right` | Memindahkan insert terpilih |
| Ganti nama dan warna | `F2` | Mengganti nama dan warna insert terpilih |
| Solo | `S` | Men-solo insert terpilih |
| Solo dengan sumber | `Alt+S` | Men-solo insert terpilih dan insert yang mengumpannya |
| Menghapus insert | `Del` | Menghapus insert terpilih, setelah bertanya |
| Siaga dan rekam | `Shift+R` | Menyiagakan insert terpilih dan memulai rekaman |
| Open audio editor | `Ctrl+E` | Memuat Edison ke slot kosong pertama insert dan membukanya |
| Open audio editor and record | `Shift+E` | Sama, siap merekam saat lagu diputar |

## Baik diketahui

- `Pengukur loudness di samping Master (LYNX)`, di `Opsi › Mixer`, menampilkan loudness jangka pendek dan terintegrasi (LUFS) serta true peak dari output Master, dengan tombol `Reset`.
- Plugin instrumen dengan beberapa output bisa memutar tiap output di insert-nya sendiri. Lihat [Plugin pihak ketiga](/wiki/plugins/).
- `Ctrl+L` melakukan hal berbeda di Channel Rack dan di Mixer. Di rack ia memilih insert kosong. Di Mixer ia memakai insert terpilih.
- Sakelar rute muncul di strip lain, bukan di strip yang terpilih.
- Batasnya 127 insert per proyek dan sepuluh slot efek per insert.
- Rekaman Edison disimpan sebagai file WAV di folder proyek, bukan di dalam file proyek.
- Mixer Session di bawah grid clip hanya milik alur kerja Clip-launch. Ia menampilkan insert yang sama dengan Mixer ini. Lihat [Layout](/wiki/layouts/).

## Terkait

- [Channel Rack](/wiki/channel-rack/)
- [Merekam](/wiki/recording/)
- [Otomasi](/wiki/automation/)
- [Plugin pihak ketiga](/wiki/plugins/)
- [Instrumen dan efek](/instruments-and-effects/)
