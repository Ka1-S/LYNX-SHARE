---
source: 2e33c00ef4
---
![Jendela Rekaman menanyakan apa yang direkam: audio, not dan otomasi, semuanya, atau audio ke editor audio](/assets/img/wiki/record-dialog.webp)

Tombol rekam di transport, atau tombol `R`, menyalakan perekaman. Pada pertama kali, sebuah jendela menanyakan apa yang ingin kamu rekam. Audio masuk ke [Playlist](/wiki/playlist/) sebagai clip audio. Not masuk ke pattern saat ini. Kontrol yang kamu gerakkan menjadi otomasi.

## Merekam mikrofon atau instrumen

1. Buka `Opsi › Pengaturan audio...` dan pilih perangkat input.
2. Buka [Mixer](/wiki/mixer/) (`F9`) dan pilih insert kosong.
3. Klik kotak input di atas slot efek insert dan pilih sebuah input. Pasangan stereo muncul lebih dulu, channel tunggal ditandai `(mono)`.
4. Klik tombol siaga rekam di sebelah kotak input.
5. Tekan `R` dan pilih `Audio, ke Playlist sebagai clip audio`.
6. Tekan play kalau pemutaran tidak mulai sendiri, lalu tampillah.
7. Tekan stop. Take-nya menjadi clip audio di Playlist.

`Shift+R` melakukan langkah 4 sampai 6 dengan satu tombol: menyiagakan insert terpilih, merekam hanya audio, dan memulai pemutaran. Tekan lagi untuk berhenti.

## Yang ditawarkan jendela Rekaman

| Pilihan | Fungsinya |
|---|---|
| `Audio, ke Playlist sebagai clip audio` | Menyiagakan insert mixer terpilih dan menanyakan inputnya kalau belum punya |
| `Not dan otomasi` | Merekam not dari keyboard MIDI atau keyboard ketik, dan kontrol yang kamu gerakkan |
| `Semuanya` | Not, otomasi, dan audio dari insert yang disiagakan |
| `Audio, ke editor audio` | Merekam input ke editor audio baru selagi lagu diputar |

Centang `Jangan tanya lagi` untuk mempertahankan pilihanmu. Setelah itu, klik kanan tombol rekam untuk filter rekaman: `Otomasi`, `Not`, `Audio`, `Clip`, dan `Tanyakan apa yang direkam saat rekaman dimulai` untuk memunculkan jendela itu lagi.

## Kotak input sebuah insert

![Mixer dengan insert 3 disiagakan dan kotak inputnya diatur ke input 1 dan 2](/assets/img/wiki/mixer-input.webp)

| Pengaturan | Fungsinya |
|---|---|
| `Sumber input audio` | Input perangkat yang diputar dan direkam insert ini, atau `(tidak ada)` |
| `Rekam` | Tempat take diambil: `Hanya input eksternal`, `Input eksternal dan mixer`, `Setelah efek`, `Setelah EQ`, `Setelah level dan panning`, atau `Setelah track` |
| `Rekam juga input kering sebagai take kedua (LYNX)` | Menyimpan input yang belum diproses di samping take yang sudah diproses |
| `Monitor input eksternal` | `Mati`, `Saat disiagakan`, `Otomatis (saat disiagakan, tidak saat memutar)`, atau `Aktif` |
| `Aman rekam (tidak bisa disiagakan)` | Melindungi insert agar tidak tersiagakan secara tidak sengaja |

## Merekam not

Not berasal dari keyboard MIDI, keyboard ketik (`Ctrl+T`) atau keyboard sentuh, dan masuk ke channel terpilih di pattern saat ini. Lihat [Controller MIDI](/wiki/midi-controllers/).

- `Campur not rekaman` (`Ctrl+B`) menentukan antara overdub dan replace. Aktif, not baru bergabung dengan not pattern. Mati, not lama pada channel yang direkam dihapus.
- `Kuantisasi not rekaman ke snap global` ada di opsi rekaman.
- `Input per langkah` (`Ctrl+E`) bekerja saat berhenti: setiap not yang kamu mainkan masuk ke pattern, satu langkah snap demi satu.
- `Tangkap MIDI` (`Ctrl+Shift+Alt+R`) memasukkan not yang baru saja kamu mainkan ke pattern, walaupun perekaman sedang mati.

Kontrol yang kamu gerakkan saat merekam ditulis sebagai clip otomasi dalam mode Song dan sebagai event pattern dalam mode Pattern. Lihat [Otomasi](/wiki/automation/).

## Opsi rekaman

Opsinya ada di `Opsi › Rekaman` dan di menu klik kanan tombol rekam.

| Perintah | Tombol | Fungsinya |
|---|---|---|
| `Hitungan awal` | `Ctrl+P` | Mati, 1 bar, 2 bar, atau 4 bar metronom sebelum rekaman dimulai |
| `Metronom` | `Ctrl+M` | Klik selama memutar |
| `Tunggu not pertama untuk mulai` | `Ctrl+I` | Rekaman menunggu sampai kamu bermain |
| `Rekam berulang` | tidak ada | Rekaman berulang, bukan berjalan terus |
| `Audio: punch in/out di region loop` | tidak ada | Memutar melewati region loop dan hanya menyimpan audio di dalamnya |
| `Audio: pre-roll` dan `post-roll` | tidak ada | Mati, 1, 2, atau 4 bar diputar sebelum punch-in dan sesudah punch-out |
| `Rekam memulai pemutaran` | tidak ada | Menyalakan perekaman juga menekan play |
| `Batalkan siaga insert saat rekaman berhenti` | tidak ada | Insert yang disiagakan dilepas setelah setiap take |

## Rekam loop dan take

Dengan `Rekam berulang` aktif, perekaman berulang di region loop dalam mode Song, atau di pattern dalam mode Pattern. Saat mati, perekaman terus berjalan lurus.

Setiap putaran rekam loop audio mendarat di track Playlist kosong berikutnya, dan track-track itu membentuk sebuah grup. `Campur not rekaman` menentukan apa yang diputar setelah kamu berhenti: mati me-mute setiap putaran, aktif memutar semuanya. Klik kanan sebuah track dalam grup dan pilih `Buat comp dari take ini` untuk menyusun satu take dari putaran-putaran itu. `Setelah rekam loop, hanya putaran terakhir yang diputar` di opsi rekaman kembali ke satu track.

## Tempat rekaman disimpan

- Proyek yang tersimpan menyimpan take-nya di `Audio/Recorded`, di sebelah file proyek.
- Proyek yang belum disimpan merekam ke `LYNX/Recordings` di folder Music-mu.
- Take berupa file WAV pada sample rate perangkat audio, 24 bit atau 32 bit float. Format dan nama take diatur di bawah `Rekaman` di `Opsi › Pengaturan audio...`.
- `Buka folder rekaman` ada di menu klik kanan tombol rekam. `File › Hapus rekaman tak terpakai dan simpan...` menghapus take yang tidak lagi dipakai proyek.

## Baik diketahui

- Insert yang disiagakan tanpa input tidak merekam apa pun. LYNX SHARE memberitahumu dan menunjuk ke kotak input.
- Perekaman menolak mulai kalau drive punya ruang kurang dari 30 detik, dan memperingatkan di bawah 10 menit.
- `Opsi › Rekam audio input` merekam dua channel input pertama ke insert baru saat tidak ada yang disiagakan.
- Di macOS, sistem meminta akses mikrofon pada pertama kali kamu merekam.
- Kalau LYNX SHARE tertutup saat merekam, saat dijalankan berikutnya program menawarkan untuk menaruh take yang dipulihkan di Playlist.

## Terkait

- [Pengaturan audio](/wiki/audio-setup/)
- [Mixer](/wiki/mixer/)
- [Otomasi](/wiki/automation/)
- [Controller MIDI](/wiki/midi-controllers/)
- [Pemecahan masalah](/wiki/troubleshooting/)
