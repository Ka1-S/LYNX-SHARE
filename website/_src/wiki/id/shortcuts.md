---
source: 5efc5c8254
---
![Jendela Kelola pintasan: daftar aksi yang bisa dicari beserta tombol masing-masing](/assets/img/wiki/shortcuts.webp)

Ini adalah tombol default di 0.1.25 alpha. `Bantuan › Pintasan` (`F1`) menampilkan daftar pintasan di dalam aplikasi. `Opsi › Kelola pintasan...` membuka editor pada gambar, tempat kamu mengubahnya.

> Di macOS aplikasi menampilkan tombol dengan nama Mac: Ctrl ditampilkan sebagai ⌘ dan Alt sebagai ⌥.

## Mengubah pintasan

1. Pilih `Opsi › Kelola pintasan...`.
2. Ketik aksi atau tombol, misalnya `Ctrl+L`, di kotak pencarian. Daftar di sebelahnya membatasi pencarian ke satu tempat, dan `Hanya yang diubah` menampilkan yang sudah kamu ubah.
3. Klik aksinya.
4. Klik `Ubah` untuk mengganti tombolnya, atau `Tambah pintasan` untuk memberinya yang kedua, lalu tekan tombol barunya. `Esc` membatalkan.
5. Jika tombol itu sudah dipakai di tempat yang sama, sebuah pesan menyebutkan aksi lainnya dan memungkinkan kamu memindahkannya.

`Hapus` menghapus tombol sebuah aksi. `Reset ke bawaan` memulihkan satu aksi dan `Reset semua...` memulihkan semuanya. Perubahanmu disimpan di `keys.json` di folder pengaturan (`%APPDATA%\LYNX` di Windows, `~/Library/Application Support/LYNX` di macOS, `~/.config/LYNX` di Linux). `Buka keys.json` membuka file itu.

## Transport dan rekaman

| Tombol | Fungsinya |
|---|---|
| `Space` | Putar, atau berhenti dan kembali ke tempat pemutaran dimulai |
| `Ctrl+Space` | Putar atau jeda |
| `Shift+Space` | Putar, atau berhenti di awal song atau pattern |
| `L` | Berpindah antara mode Pattern dan mode Song |
| `R` | Rekaman aktif atau mati |
| `Shift+R` | Rekam audio sekarang: mengaktifkan insert terpilih dan merekam |
| `Ctrl+M` | Metronom aktif atau mati |
| `Ctrl+P` | Hitungan awal sebelum merekam aktif atau mati |
| `Ctrl+I` | Rekaman menunggu not pertama, aktif atau mati |
| `Ctrl+B` | Blend not yang direkam aktif atau mati |
| `Ctrl+E` | Input per langkah aktif atau mati |
| `Ctrl+H` | Hentikan suara (matikan semua not) |
| `Ctrl+T` | Keyboard ketik menjadi piano, aktif atau mati |
| `Alt+Numpad *` / `Alt+Numpad /` | Lompat ke marker song berikutnya atau sebelumnya |

Beberapa tombol ini melakukan hal lain di dalam editor yang sedang fokus. Di Piano Roll, `Ctrl+M` mengimpor file MIDI dan `Ctrl+B` menduplikasi. Di Playlist, `Ctrl+P` adalah mode Performance.

## File dan penyuntingan

| Tombol | Fungsinya |
|---|---|
| `Ctrl+N` | Proyek baru |
| `Ctrl+O` | Buka proyek |
| `Ctrl+S` | Simpan |
| `Ctrl+Shift+S` | Simpan sebagai |
| `Ctrl+Alt+S` | Simpan versi baru |
| `Ctrl+R` | Ekspor audio |
| `Ctrl+Shift+R` | Ekspor file MP3 |
| `Ctrl+Shift+M` | Ekspor file MIDI |
| `Ctrl+Z` | Urungkan. Ditekan lagi, ia mengulangi langkah itu |
| `Ctrl+Alt+Z` | Urungkan mundur, langkah demi langkah |
| `Ctrl+Y` atau `Ctrl+Shift+Z` | Ulangi |
| `Ctrl+Alt+H` | Riwayat urungkan |
| `Ctrl+F` | Cari di Browser |

## Jendela

| Tombol | Fungsinya |
|---|---|
| `F5` | Playlist |
| `F6` | Channel Rack |
| `F7` | Piano Roll |
| `F9` | Mixer |
| `Alt+F8` | Browser |
| `F8` | Pemilih plugin |
| `Shift+F8` | Database plugin (di Browser) |
| `Enter` | Maksimalkan atau pulihkan Playlist |
| `F10` | Pengaturan MIDI |
| `F11` | Info proyek |
| `F12` | Tutup semua jendela (plugin, pengaturan channel, alat) |
| `Alt+F12` | Tutup semua jendela plugin |
| `Ctrl+F12` | Tutup semua jendela kecuali yang sedang fokus |
| `Alt+F7` | Keyboard sentuh |
| `Ctrl+F5` | Tempo tapper |
| `F1` | Daftar pintasan |

## Pattern

| Tombol | Fungsinya |
|---|---|
| `Numpad +` / `Numpad -` | Pattern berikutnya, sebelumnya |
| `Numpad 1` sampai `Numpad 9` | Pilih pattern 1 sampai 9 |
| `F4` | Pattern kosong berikutnya, dan beri nama |
| `Ctrl+F4` | Pattern kosong berikutnya, tanpa memberi nama |
| `Shift+F4` | Pattern kosong pertama, dan beri nama |
| `F2` | Ganti nama dan warna pattern |
| `Ctrl+Shift+C` | Kloning pattern |
| `Ctrl+Shift+Ins` | Sisipkan pattern kosong sebelum pattern saat ini |
| `Ctrl+Shift+Del` | Hapus pattern |
| `Ctrl+Shift+Up` / `Ctrl+Shift+Down` | Pindahkan pattern ke atas, ke bawah |

## Channel Rack

| Tombol | Fungsinya |
|---|---|
| `Up` / `Down` | Pilih channel di atas, di bawah |
| `Shift+Up` / `Shift+Down` | Perluas pilihan |
| `Alt+Up` / `Alt+Down` | Pindahkan channel terpilih |
| `Ctrl+A` | Pilih semua channel |
| `Ctrl+X` / `Ctrl+C` / `Ctrl+V` | Potong, salin, tempel step milik channel terpilih |
| `Ctrl+Shift+Left` / `Ctrl+Shift+Right` | Putar step ke kiri, ke kanan |
| `Alt+C` | Kloning channel terpilih |
| `Alt+Del` | Hapus channel terpilih |
| `Alt+G` | Kelompokkan channel terpilih |
| `Ctrl+L` | Kirim channel terpilih ke insert Mixer yang kosong |
| `Ctrl+K` | Jalur grafik step aktif atau mati |
| `1` sampai `0` | Mute atau batalkan mute channel 1 sampai 10 |
| `Ctrl+1` sampai `Ctrl+0` | Solo channel 1 sampai 10 |

## Piano Roll

| Tombol | Fungsinya |
|---|---|
| `P` `B` `N` `D` `T` `C` `E` `Z` `Y` | Pensil, Kuas, Kuas drum, Penghapus, Mute, Pisau, Laso, Zoom, Dengarkan |
| `S` | Not slide aktif atau mati (untuk not baru) |
| `G` / `K` | Channel sebelumnya, berikutnya |
| `H` / `J` | Channel sebelumnya, berikutnya yang punya not |
| `Ctrl+A` | Pilih semua |
| `Ctrl+D` atau `Esc` | Batalkan pilihan |
| `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | Salin, potong, tempel |
| `Ctrl+B` | Duplikat |
| `Del` | Hapus pilihan |
| `Up` / `Down` | Geser pilihan satu semitone |
| `Ctrl+Up` / `Ctrl+Down` | Geser pilihan satu oktaf |
| `Left` / `Right` | Geser pilihan satu langkah grid |
| `Ctrl+Shift+Left` / `Ctrl+Shift+Right` | Putar not dalam bar |
| `Backspace` | Snap mati atau aktif |
| `Ctrl+Q` | Kuantisasi cepat (awal dan panjang) |
| `Shift+Q` | Kuantisasi awal |
| `Ctrl+U` | Cacah ke grid |
| `Ctrl+G` | Gabungkan not yang bersentuhan |
| `Ctrl+L` | Legato |
| `Shift+D` | Pendekkan not ke grid |
| `Ctrl+P` | Porta aktif atau mati untuk pilihan |
| `Alt+M` / `Shift+Alt+M` | Mute, batalkan mute pilihan |
| `Alt+V` | Ghost note aktif atau mati |
| `Shift+I` | Balik pilihan |
| `Shift+G` / `Alt+G` | Kelompokkan, bubarkan kelompok not terpilih |
| `Alt+C` | Warnai not terpilih dengan warna not |
| `Shift+C` | Pilih not dengan warna yang sama |
| `Shift+N` | Stempel chord: tidak ada (not tunggal) |
| `Ctrl+Ins` / `Ctrl+Del` | Sisipkan, hapus ruang (pilihan waktu) |
| `Ctrl+M` | Impor file MIDI |
| `Ctrl+Shift+M` | Ekspor sebagai file MIDI |
| `PageUp` / `PageDown` | Perbesar, perkecil |
| `Shift+1` / `Shift+2` / `Shift+3` | Zoom ke 1, 4, 16 bar |
| `Shift+4` | Zoom untuk menampilkan semuanya |
| `Shift+5` | Zoom ke pilihan |
| `Shift+0` | Pusatkan tampilan pada playhead |

### Alat not

Setiap tombol membuka dialog satu alat not di Piano Roll.

| Tombol | Fungsinya |
|---|---|
| `Alt+A` | Arpeggio |
| `Alt+S` | Strum |
| `Alt+Y` | Balik |
| `Alt+R` | Acak |
| `Alt+X` | Skala level |
| `Alt+K` | Batasi ke rentang |
| `Alt+Q` | Kuantisasi |
| `Alt+U` | Cacah |
| `Alt+F` | Flam |
| `Alt+W` | Claw machine |
| `Alt+O` | LFO |
| `Alt+L` | Artikulasi |
| `Alt+E` | Riff machine |
| `Alt+P` | Buat progresi chord |

## Playlist

| Tombol | Fungsinya |
|---|---|
| `P` `B` `D` `T` `C` `E` `Z` `Y` `S` | Pensil, Kuas, Penghapus, Mute, Pisau, Laso, Zoom, Dengarkan, Slip |
| `Ctrl+A` | Pilih semua |
| `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | Salin, potong, tempel |
| `Ctrl+B` | Duplikat |
| `Del` | Hapus clip terpilih |
| `Shift+Del` | Hapus clip terpilih dan tutup celahnya |
| `Shift+S` | Ripple editing aktif atau mati |
| `Backspace` | Snap mati atau aktif |
| `Alt+M` / `Shift+Alt+M` | Mute, batalkan mute clip terpilih |
| `Shift+M` | Stretch audio saat mengubah ukuran clip audio, aktif atau mati |
| `Ctrl+Shift+V` | Jadikan clip terpilih unik |
| `Ctrl+G` | Gabungkan klip pattern |
| `Ctrl+Shift+G` | Gabungkan klip pattern yang mirip |
| `Ctrl+Alt+G` | Gabungkan klip otomasi |
| `Ctrl+Shift+B` | Kloning track Playlist terpilih |
| `Ctrl+Q` | Kuantisasi awal clip |
| `7` / `8` | Turunkan, naikkan pitch clip audio terpilih satu semitone |
| `9` | Balik clip audio terpilih |
| `Ctrl+Ins` / `Ctrl+Del` | Sisipkan, hapus sepanjang pilihan waktu di semua track |
| `Ctrl+Left` / `Ctrl+Right` | Pilih waktu sebelumnya, berikutnya |
| `Ctrl+Enter` | Pilih waktu di sekitar pilihan |
| `Alt+T` | Tambah marker waktu |
| `Ctrl+T` | Tambah marker otomatis |
| `Shift+Alt+T` | Tambah perubahan birama |
| `Shift+T` | Tempatkan marker loop |
| `Ctrl+Alt+C` | Konsolidasi pilihan Playlist |
| `Ctrl+P` | Mode performance aktif atau mati |
| `PageUp` / `PageDown` | Perbesar, perkecil |
| `Shift+1` / `Shift+2` / `Shift+3` | Zoom ke 1, 4, 16 bar |
| `Shift+4` | Zoom untuk menampilkan semuanya |
| `Shift+5` | Zoom ke pilihan |
| `Shift+0` | Pusatkan tampilan pada playhead |

## Mixer

| Tombol | Fungsinya |
|---|---|
| `Alt+Left` / `Alt+Right` | Pindahkan insert terpilih ke kiri, ke kanan |
| `F2` | Ganti nama dan warna insert terpilih |
| `S` | Solo insert terpilih |
| `Alt+S` | Solo insert terpilih beserta insert yang mengumpannya |
| `Ctrl+L` | Tautkan channel terpilih ke insert terpilih |
| `Ctrl+Shift+L` | Tautkan channel terpilih ke insert mulai dari insert terpilih |
| `Alt+L` | Pilih channel yang tertaut ke insert terpilih |
| `Del` | Hapus plugin di slot efek terpilih |
| `Ctrl+E` | Buka editor audio |
| `Shift+E` | Buka editor audio dan rekam |
| `Alt+R` | Render insert yang diaktifkan ke file wave |
| `Shift+Alt+R` | Render insert terpilih ke file wave |

## Browser

| Tombol | Fungsinya |
|---|---|
| `Up` / `Down` | File sebelumnya, berikutnya (dengan pratinjau) |
| `Shift+Up` / `Shift+Down` | File sebelumnya, berikutnya ke channel terpilih |
| `Enter` | Muat file ke channel terpilih |
| `Left` | Tutup folder, atau naik ke folder induk |
| `Right` | Buka folder |
| `F2` | Ganti nama plugin terpilih (Database plugin) |

## Perlu diketahui

- Selagi piano keyboard ketik aktif, tombolnya memainkan not dan sebagian besar pintasan satu huruf dijeda (`L` tetap bekerja). Matikan dengan `Ctrl+T`.
- Sebuah pintasan bekerja di mana saja atau di satu jendela. Pintasan milik sebuah jendela menang atas tombol yang sama yang berlaku di mana saja, selagi jendela itu fokus.
- Beberapa aksi tidak punya tombol default, misalnya Palet tambah cepat. Beri tombol di editor.

## Terkait

- [Tur jendela](/wiki/tour/)
- [Pengaturan](/wiki/settings/)
- [Controller MIDI](/wiki/midi-controllers/)
- [Pindahan dari FL Studio](/wiki/coming-from-fl-studio/)
