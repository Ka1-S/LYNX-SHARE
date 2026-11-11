---
source: b2de12e24d
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

Memindahkan, menyisipkan, atau menghapus insert membuat send, sidechain, dan otomasi tetap menempel pada insert-nya.

## Mute dan solo

Klik lampu mute untuk me-mute insert. Klik kanan atau `Ctrl`+klik untuk solo. `S` men-solo insert terpilih, dan `Alt+S` men-solo-nya bersama insert yang mengumpannya.

## Efek

Panel efek menampilkan sepuluh slot dari insert terpilih.

- Seret sebuah slot, atau pakai roda di atasnya, untuk memindahkannya ke atas atau ke bawah.
- Klik kanan sebuah slot untuk opsinya (ganti, pindah, hapus).
- `Del` menghapus plugin di slot terpilih.
- Sakelar di header panel menyalakan atau mematikan semua efek insert itu.
- Kamu bisa menyeret efek dari database Plugin di [Browser](/wiki/browser/) ke sebuah insert atau slot efek.

## EQ insert

Setiap insert punya EQ sendiri. Seret sebuah band untuk mengatur frekuensi dan levelnya, pakai roda untuk lebarnya, dan klik dua kali sebuah band untuk mengembalikannya ke 0 dB. Klik kanan judul EQ untuk mem-bypass-nya. Kalau kamu ingin Mixer tanpa EQ bawaan, proyek baru bisa diatur untuk dimulai dengan mixer bersih.

## Routing, send, dan sidechain

![Mixer dengan insert Kick terpilih, send-nya digambar sebagai kabel di bawah strip dan panel efek di kanan](/assets/img/wiki/mixer-sends.webp)

Pilih insert yang ingin kamu arahkan. Setiap strip lain lalu punya sakelar rute:

- Klik sakelar di strip lain untuk mengirim insert terpilih ke sana. Menyeret setelah mengaktifkan mengatur level send.
- `Ctrl`+klik mengarahkan ke insert itu saja.
- `Shift`+klik membuat send sidechain. `Ctrl+Shift`+klik membuatnya khusus sidechain.
- Klik kanan membuka pilihan yang sama sebagai menu.

Efek dengan input samping, seperti compressor, punya pemilih `Sidechain dari` yang mencantumkan insert yang bisa didengarkan. Memilih salah satunya membuatkan rutenya.

## Grup dan fader VCA

Grup Mixer dilipat dengan chevron-nya (atau `Alt`+klik untuk semua grup). Fader VCA menggerakkan level insert anggotanya selagi mereka tetap punya fader sendiri. Buat dari menu insert: di bawah Group, pilih `VCA › VCA baru dari yang dipilih`.

## Merekam

Setiap insert punya tombol siaga rekam. Dengan insert yang disiagakan, Rec dan Play merekam input audionya ke clip Playlist. `Shift+R` menyiagakan insert terpilih dan memulai rekaman dalam satu langkah. Lihat [Merekam](/wiki/recording/).

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
| Menghapus efek | `Del` | Menghapus plugin di slot terpilih |
| Siaga dan rekam | `Shift+R` | Menyiagakan insert terpilih dan memulai rekaman |

## Baik diketahui

- `Render ke audio` (insert yang di-bounce menjadi clip audio) diputar sekitar 5 dB lebih keras daripada insert yang digantikannya. Ini masalah yang sudah diketahui di catatan rilis 0.1.25.
- `Ctrl+L` melakukan hal berbeda di Channel Rack dan di Mixer. Di rack ia memilih insert kosong. Di Mixer ia memakai insert terpilih.
- Sakelar rute muncul di strip lain, bukan di strip yang terpilih.
- Batasnya 127 insert per proyek dan sepuluh slot efek per insert.

## Terkait

- [Channel Rack](/wiki/channel-rack/)
- [Merekam](/wiki/recording/)
- [Otomasi](/wiki/automation/)
- [Plugin pihak ketiga](/wiki/plugins/)
- [Instrumen dan efek](/instruments-and-effects/)
