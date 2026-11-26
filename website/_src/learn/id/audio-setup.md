---
source: 297095d385
---
## Driver dan output

![Halaman Audio di Pengaturan](/assets/img/learn/audio-setup-1.webp)

1. Buka `Opsi › Pengaturan audio...`.
2. Pilih driver. Di Windows, `LYNX ASIO (bawaan)` bekerja dengan perangkat apa pun dan tidak butuh driver sendiri; driver ASIO milik interface-mu, FL Studio ASIO, dan `Windows Audio (bersama)` atau `Windows Audio (eksklusif, latensi terendah)` juga tersedia. Core Audio di Mac, PipeWire atau ALSA di Linux.
3. `Perangkat output` adalah tempat kamu mendengar LYNX. `System default` mengikuti output yang dipakai komputermu.
4. `Perangkat input` adalah sumber yang kamu rekam (mikrofon, input gitar). `Sample rate` biasanya 44100 atau 48000 Hz.

## Ukuran buffer

![Ukuran buffer beserta latensinya](/assets/img/learn/audio-setup-2.webp)

1. `Ukuran buffer`: buffer yang lebih kecil lebih cepat merespons saat kamu bermain keyboard; yang lebih besar memberi CPU lebih banyak ruang.
2. Berderak atau suara putus-putus? Naikkan. Nada terlambat saat kamu bermain? Turunkan.

## Tes suara

![Help, Test audio output](/assets/img/learn/audio-setup-3.webp)

1. `Putar suara uji` memutar suara pendek lewat output yang dipilih.
2. Dari mana saja, `Bantuan › Uji output audio...` melakukan hal yang sama.

## Take rekaman terlambat?

1. Interface-mu butuh sejenak untuk memasukkan suara lalu mengeluarkannya lagi, jadi sebuah take bisa mendarat sedikit terlambat.
2. Di halaman Audio yang sama, di bawah `Kompensasi latensi rekaman`, sambungkan sebuah output ke sebuah input dengan kabel (atau pakai loopback interface-mu) lalu klik `Ukur round trip`.
3. LYNX memutar beberapa klik, mengukur seberapa terlambat klik itu kembali, dan sejak itu menaruh take rekaman tepat waktu.

Lihat juga [Pengaturan audio](/wiki/audio-setup/) dan [Pemecahan masalah](/wiki/troubleshooting/).
