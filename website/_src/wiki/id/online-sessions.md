---
source: 27354bbca9
---
Ada dua cara mengerjakan proyek bersama orang lain, dan keduanya ada di `File › Sesi online`. Paket adalah file yang kamu kirim ke teman, yang lalu mengirim sebuah bagian kembali. Tidak butuh jaringan, akun, atau server. Sesi live menghubungkan semua orang lewat internet melalui relay LYNX SHARE, dan setiap perubahan sampai ke semua orang. Hingga 4 orang ikut dalam satu proyek, termasuk kamu.

## Mengirim proyek ke teman

![Kirim proyek](/assets/img/wiki/session-send.webp)

1. Buka proyek dan pilih `File › Sesi online › Kirim proyek...`.
2. Ketik namamu dan nama temanmu.
3. Pilih apa yang disimpan temanmu saat sesi berakhir: `Hanya bagiannya sendiri (disarankan)` atau `Salinan lengkap proyek`.
4. Simpan file `.lynxpack`.
5. Kirim file itu dengan cara apa pun: e-mail, folder bersama, flash disk.

Paket berisi proyek, semua sample dan rekaman yang dipakainya, serta render audio ("stand-in") dari bagian yang memakai plugin pihak ketiga. Temanmu mendengar bagian-bagian itu tanpa memiliki plugin-mu.

## Merekam sebuah bagian dan mengirimnya kembali

Temanmu melakukan ini:

1. Buka `.lynxpack` dengan `File › Buka...`. Sebuah ringkasan menunjukkan siapa yang mengirim dan insert mixer serta track Playlist mana yang menjadi milikmu. Klik `Buka`.
2. Tambahkan channel, pattern, dan audiomu sendiri pada insert dan track Playlist milikmu. Materi host dibiarkan apa adanya.
3. Pilih `File › Sesi online › Kirim balik bagianku...` lalu kirim paket baru itu ke host.

Setiap tamu mendapat sebuah warna, 20 insert mixer, dan 16 track Playlist.

## Menggabungkan sebuah bagian

Host melakukan ini:

1. Buka proyek yang tadi dikirim.
2. Pilih `File › Sesi online › Gabungkan bagian...` lalu pilih paket yang dikirim kembali temanmu.
3. Channel, pattern, not, clip, dan insert temanmu ditambahkan dengan warnanya. Tidak ada milikmu yang diubah.

Penggabungan adalah satu langkah urungkan. Bagian yang lebih baru dari teman yang sama menggantikan yang sebelumnya.

## Memulai atau bergabung ke sesi live

![Mulai sesi live](/assets/img/wiki/session-start.webp)

Host memulai sesi:

1. Buka proyek dan pilih `File › Sesi online › Mulai sesi live...`.
2. Ketik namamu.
3. Di bawah `Tamu boleh menyunting`, pilih `Hanya channel, pattern, insert, dan barisnya sendiri (disarankan)` atau `Semuanya`. Di bawah `Saat sesi berakhir, tamu menyimpan`, pilih bagian mereka sendiri atau salinan lengkap.
4. Klik `Awal`, lalu salin kode undangan dan kirim ke hingga 3 orang.
5. Saat seseorang meminta bergabung, klik `Izinkan masuk`.

Tamu bergabung:

1. Pilih `File › Sesi online › Gabung ke sesi live...`.
2. Ketik namamu, tempel kode undangan, lalu klik `Gabung`.
3. Tunggu sampai host mengizinkanmu masuk. Proyek host terbuka.

## Selama sesi live

![Obrolan sesi](/assets/img/wiki/session-chat.webp)

Menu `File › Sesi online` punya perintah berikut selama sesi berjalan:

| Perintah | Fungsinya |
|---|---|
| `Obrolan sesi...` | Membuka obrolan. Item menu menampilkan jumlah pesan yang belum dibaca |
| `Ikuti transport host` | Untuk tamu: play, stop, dan posisi mengikuti host |
| `Orang dan izin...` | Untuk host: apa yang boleh diubah tiap orang, dan apa yang disimpan masing-masing di akhir |
| `Tampilkan kode undangan...` | Untuk host: menampilkan kode lagi, dengan tombol `Salin` |
| `Akhiri sesi live` | Untuk host. Semua orang keluar, dan kodenya berhenti berlaku |
| `Keluar dari sesi live` | Untuk tamu. Bagianmu dari proyek tetap bersamamu |

Di `Orang dan izin...`, setiap orang mendapat salah satu dari empat peran: `Channel, pattern, insert, dan barisnya sendiri`, `Bagiannya sendiri + tempo dan pengaturan proyek`, `Semuanya`, atau `Hanya mendengarkan (tanpa suntingan)`. Mematikan sebuah izin tidak pernah menghapus apa pun.

## Plugin yang tidak dimiliki orang lain

Channel atau insert yang pluginnya tidak terpasang di komputermu memainkan stand-in, dan menampilkan lencana `R` kuning di Channel Rack dan Mixer.

- Dalam paket, stand-in adalah render audio yang dibuat saat paket disimpan. Stand-in mengikuti clip yang kamu pindahkan, tetapi tidak mengikuti perubahan not dari plugin yang tidak kamu punya. Minta pemiliknya mengirim paket baru.
- Dalam sesi live, bagian yang memakai plugin yang tidak dimiliki seseorang di-render oleh orang yang memilikinya dan di-stream ke yang lain sebagai audio.

## Apa yang lewat jaringan

Paket adalah file. Tidak ada yang diunggah ke mana pun.

Sesi live hanya terhubung selama kamu berada di dalamnya. Semuanya lewat relay `relay.lynx-share.com`, dienkripsi end-to-end dengan kunci yang berasal dari kode undangan. Relay meneruskan data tanpa bisa membacanya. Relay melihat alamat IP-mu, kapan kamu terhubung, dan berapa byte yang lewat. Orang lain di sesi melihat namamu, suntinganmu, sample yang dipakai bagianmu, dan apakah kamu sedang memutar atau merekam. Mereka tidak melihat alamat IP-mu. Rinciannya ada di [kebijakan privasi](https://github.com/Ka1-S/LYNX-SHARE/blob/main/PRIVACY.md).

## Baik diketahui

- `Hanya bagiannya sendiri` adalah kesopanan antar musisi, bukan perlindungan salin. Paketnya zip biasa.
- Kedua pihak sebaiknya memakai versi LYNX SHARE yang sama. Ringkasan paket memberi peringatan kalau berbeda.
- Tamu yang koneksinya putus tersambung kembali sendiri dan suntingannya tetap ada.
- Kalau LYNX SHARE milik host tertutup selama sesi, saat dijalankan berikutnya program menawarkan untuk melanjutkannya, dan para tamu tersambung kembali sendiri.
- Dalam sesi live, urungkan dan ulangi hanya mengembalikan suntinganmu sendiri.
- Siapa pun yang punya kode undangan bisa meminta bergabung, tetapi hanya setelah host mengizinkan mereka masuk.

## Terkait

- [Yang hanya dilakukan LYNX SHARE: sesi online](/why/#online)
- [Format file](/wiki/file-formats/)
- [Plugin pihak ketiga](/wiki/plugins/)
- [Urungkan dan riwayat](/wiki/undo-history/)
- [Pemecahan masalah](/wiki/troubleshooting/)
