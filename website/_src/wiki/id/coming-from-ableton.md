---
source: d5cc625221
---
![Layout Clip-launch: kisi klip dengan scene di samping Playlist](/assets/img/cliplaunch.webp)

LYNX SHARE dimulai dalam alur kerja pattern: Channel Rack, pattern, dan Playlist. Alur kerja peluncuran klip bersifat opsional dan menambahkan kisi klip dengan scene di samping Playlist. LYNX SHARE membaca dan menulis set `.als`. Program ini tidak berafiliasi dengan Ableton.

## Membuka set Live

1. Pilih `File › Buka...` (`Ctrl+O`) dan pilih file `.als`. Set yang disimpan Live 11 dan Live 12 bisa dibaca.
2. Pilih `Tampilan › Gaya tata letak › Alur kerja peluncuran klip` untuk bekerja dengan kisi klip, tempat clip Session dan scene dari set itu ditaruh.
3. Pakai sakelar `Grid / Both / Song` di bilah judul Playlist untuk menampilkan kisi, Playlist, atau keduanya.
4. Pilih `File › Simpan`. Program menanyakan tempat menulis file `.lynx`. File `.als` tidak diubah.

`Tampilan › Gaya tata letak › Alur kerja pattern (bawaan)` kembali ke semula. Pilihan yang sama ada di `Opsi › Pengaturan umum...`. Pattern dan Channel Rack tetap tersedia di kedua layout.

## Padanannya

| Di Live | Di LYNX SHARE |
|---|---|
| Session view | Kisi klip pada alur kerja peluncuran klip |
| Arrangement view | Playlist |
| Track | Track Playlist, yang menjadi kolom kisi klip |
| Scene | Scene, baris-baris kisi klip. Sebuah scene bisa mengatur tempo saat diluncurkan |
| Kuantisasi peluncuran | `Global`, `Tidak ada`, 8 bar sampai 1 bar, atau nilai not dari 1/2 sampai 1/32, per clip jika kamu mau |
| Mode peluncuran | `Pemicu`, `Gate`, `Alih`, dan `Ulang` |
| Follow action | Aksi A dan Aksi B dengan peluang masing-masing |
| Tombol stop clip | Slot kosong bisa punya tombol stop |
| Kembali ke Arrangement | `Kembali ke Playlist` |
| Mode warp | `Beat`, `Nada`, `Tekstur`, dan `Kompleks` di `Pengaturan channel › Time stretch` |
| Simpler, Sampler, dan drum rack | Sampler LYNX SHARE |
| Set `.als` | File proyek `.lynx` |

## Kisi klip

Sebuah slot berisi clip pattern atau clip audio.

- Satu track memainkan satu clip pada satu waktu. Clip yang diluncurkan menggantikan clip yang sedang diputar.
- Peluncuran menunggu step berikutnya dari kuantisasi peluncuran.
- `Legato` memulai clip baru pada posisi yang sudah dicapai clip yang digantikan. `Loop` diatur per clip.
- Scene bisa disisipkan, dihapus, dan diganti namanya.
- Track yang telah meluncurkan clip tetap senyap di Playlist sampai kamu menekan `Kembali ke Playlist`. Sebuah opsi mengembalikan track saat transport berhenti.

Follow action berjalan setelah sejumlah bar atau putaran clip. Aksinya adalah `Tanpa aksi`, `Stop`, `Putar lagi`, `Sebelumnya`, `Berikutnya`, `Pertama`, `Terakhir`, `Mana saja`, dan `Lainnya`. Sebuah sakelar menyalakan atau mematikan follow action untuk seluruh kisi.

## Yang diimpor dari file .als

- Track MIDI, audio, grup, dan return, tempo, birama, locator, loop, pengaturan mixer (volume, pan, mute, solo, send) beserta otomasinya.
- Plugin VST2 dan VST3 beserta state-nya, jika plugin yang sama terinstal. Lihat [Plugin pihak ketiga](/wiki/plugins/).
- Clip Session dan scene, ke dalam kisi klip, beserta mode peluncuran, kuantisasi, legato, loop, follow action, dan tombol stop-nya. Untuk menaruhnya di Playlist saja, satu bagian per scene, ubah `Klip sesi dari set Live` di `Opsi › Pengaturan umum...`.
- Warp marker.

## Mengekspor file .als

1. Pilih `File › Ekspor › Set Ableton Live (.als)...`.
2. Di bawah `Simpan untuk`, pilih `Live 12` atau `Live 11 (juga terbuka di Live 12)`.
3. Di bawah `Sample`, salin ke folder proyek set itu atau rujuk di lokasi aslinya.
4. Pilih folder. Set disimpan sebagai `<name> Project/<name>.als`.

| Ditulis | Tidak ditulis |
|---|---|
| Satu track MIDI per channel, dengan not-nya sebagai clip MIDI | Instrumen dan efek milik LYNX SHARE sendiri. Track dan not-nya tetap disimpan |
| Track audio dengan stretch, transpose, gain, fade, dan mode warp | Otomasi parameter efek |
| Grup, return, dan send | Rute dari insert ke insert |
| Plugin VST2 dan VST3 beserta state-nya | |
| Marker, serta otomasi volume, pan, dan tempo | |
| Kisi klip, sebagai clip Session dan scene, rack dan Drum Rack | |

Laporan setelah ekspor mencantumkan apa yang tidak ikut ditulis.

## Perlu diketahui

- Banyak device bawaan Live masuk sebagai milik LYNX SHARE sendiri, beserta pengaturan dan otomasinya: EQ Eight, EQ Three, Channel EQ, Compressor, Glue Compressor, Multiband Dynamics, Limiter, Gate, Utility, Reverb, Delay, Echo, Auto Filter, Saturator, Overdrive, Chorus-Ensemble, Redux, Auto Pan, Drum Buss, Simpler, Drift, dan Operator (tanpa FM). Sisanya dibuang dan dicantumkan di laporan impor.
- Instrument Rack dan Audio Effect Rack masuk beserta rantai, zona, dan makronya, dan Drum Rack menjadi Drum Rack milik LYNX SHARE. MIDI Effect Rack milik Live belum dikonversi dan dicantumkan di laporan impor.
- Laporan impor mencantumkan apa yang tidak ikut diimpor.
- Live memangkas clip audio yang saling tumpang tindih dalam set yang diekspor.
- Warp marker dari set Live dipertahankan saat impor dan ditulis kembali saat ekspor.

## Terkait

- [Playlist](/wiki/playlist/)
- [Tata letak](/wiki/layouts/)
- [Mengimpor proyek](/wiki/importing-projects/)
- [Format file](/wiki/file-formats/)
- [Dua alur kerja dalam satu program](/why/#workflows)
