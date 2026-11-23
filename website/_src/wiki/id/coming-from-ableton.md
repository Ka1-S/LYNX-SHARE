---
source: 37920663db
---
![Layout Clip-launch: kisi klip dengan scene di samping Playlist](/assets/img/cliplaunch.webp)

LYNX SHARE dimulai dalam alur kerja pattern: Channel Rack, pattern, dan Playlist. Alur kerja peluncuran klip bersifat opsional dan menambahkan kisi klip dengan scene di samping Playlist. LYNX SHARE membaca dan menulis set `.als`. Program ini tidak berafiliasi dengan Ableton.

Halaman ini juga untuk pengguna Bitwig Studio: kisi klip bekerja dengan cara yang sama, dan file DAWproject (`.dawproject`) dari Bitwig dibuka dengan `File › Buka...` serta ditulis oleh `File › Ekspor › DAWproject (.dawproject)...`.

## Langkah pertamamu

1. **Pilih tata letak.** Saat LYNX SHARE pertama kali dijalankan, sebuah kartu sambutan menanyakan dari mana kamu datang. Klik `Ableton Live atau Bitwig`: tata letak peluncuran klip terbuka. Nanti, `Tampilan › Gaya tata letak › Alur kerja peluncuran klip` beralih ke sana kapan saja.
2. **Isi sebuah slot.** Seret sample atau pattern dari Browser ke sebuah slot, atau klik kanan slot kosong dan pilih `Taruh pattern di sini`.
3. **Luncurkan.** Klik segitiga di kiri sebuah klip, atau segitiga sebuah scene di kolom `Scene` untuk meluncurkan seluruh barisnya. Atau buka salah satu set `.als`-mu (di bawah).

Yang berbeda dari Live:

- Playlist adalah arransemennya, dan track-nya adalah kolom-kolom kisi. Track yang sudah meluncurkan klip tetap di kisi sampai kamu menekan `Kembali ke Playlist`.
- Di luar kisi, tombolnya mengikuti FL Studio. `Ctrl+Z` membatalkan satu langkah, dan jika ditekan lagi akan mengulanginya.
- Hanya sebagian perangkat bawaan Live yang dikonversi (lihat Perlu diketahui). Plugin pihak ketiga ikut dengan statusnya.
- Tata letak Pattern, cara kerja FL Studio dengan Channel Rack dan pattern, bekerja di proyek yang sama.

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
| Satu track MIDI per channel, dengan not-nya sebagai clip MIDI | Instrumen dan efek milik LYNX SHARE sendiri, kecuali Helix, yang menjadi Wavetable. Track dan not-nya tetap disimpan |
| Track audio dengan stretch, transpose, gain, fade, dan mode warp | Otomasi parameter efek |
| Grup, return, dan send | Rute dari insert ke insert |
| Plugin VST2 dan VST3 beserta state-nya | |
| Marker, serta otomasi volume, pan, dan tempo | |
| Kisi klip, sebagai clip Session dan scene, rack dan Drum Rack | |

Laporan setelah ekspor mencantumkan apa yang tidak ikut ditulis.

## Perlu diketahui

- Banyak device bawaan Live masuk sebagai milik LYNX SHARE sendiri, beserta pengaturan dan otomasinya: EQ Eight, EQ Three, Channel EQ, Compressor, Glue Compressor, Multiband Dynamics, Limiter, Gate, Utility, Reverb, Hybrid Reverb, Delay, Echo, Grain Delay, Auto Filter, Saturator, Overdrive, Roar, Erosion, Vinyl Distortion, Corpus, Chorus-Ensemble, Redux, Auto Pan, Drum Buss, Spectral Time, Spectral Resonator, Looper, Simpler, Sampler, Drift, Operator, Wavetable, dan Meld. Sisanya dibuang dan dicantumkan di laporan impor.
- Instrument Rack dan Audio Effect Rack masuk beserta rantai, zona, dan makronya, dan Drum Rack menjadi Drum Rack milik LYNX SHARE. Arpeggiator milik Live menjadi arpeggiator channel. Pitch, Velocity, Chord, Note Length, dan MIDI Effect Rack ditulis ke dalam not.
- Laporan impor mencantumkan apa yang tidak ikut diimpor.
- Di Device View, sebuah rack menampilkan device dari rantainya secara inline, dan mode Macro Map (tombol `Map` pada rack) memberikan parameter device ke sebuah makro, seperti di Live.
- Mixer Session punya sakelar Solo / Cue milik Live, dengan `Cue Out` untuk headphone-mu dan `Volume cue`.
- Live memangkas clip audio yang saling tumpang tindih dalam set yang diekspor.
- Warp marker dari set Live dipertahankan saat impor dan ditulis kembali saat ekspor.

## Terkait

- [Playlist](/wiki/playlist/)
- [Tata letak](/wiki/layouts/)
- [Mengimpor proyek](/wiki/importing-projects/)
- [Format file](/wiki/file-formats/)
- [Dua alur kerja dalam satu program](/why/#workflows)
- [Belajar: tutorial](/learn/)
