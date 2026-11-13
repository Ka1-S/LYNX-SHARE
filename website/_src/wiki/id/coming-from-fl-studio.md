---
source: 3bf7e0c0ab
---
LYNX SHARE mempertahankan alur kerja yang sudah kamu kenal: Channel Rack, pattern, Playlist, dan Mixer, dengan tombol yang sama. Program ini membuka proyek `.flp` secara langsung dan bisa menulisnya kembali.

LYNX SHARE adalah program independen. Program ini tidak berafiliasi dengan, didukung, atau disponsori oleh Image-Line, dan tidak berisi kode Image-Line. FL Studio adalah merek dagang terdaftar milik Image-Line Software.

## Membuka proyek .flp

1. Pilih `File › Buka...` (`Ctrl+O`) lalu pilih `.flp`-nya.
2. LYNX SHARE mencari plugin dan sample proyek itu, lalu membukanya. Judulnya menampilkan "(imported from FL Studio)".
3. Pilih `Bantuan › Last FL Studio import report` untuk membaca apa yang diperkirakan, dilewati, atau tidak ditemukan.
4. Pilih `Bantuan › File hilang...` untuk melihat sample dan plugin yang hilang.
5. Pilih `File › Simpan`. Program menanyakan tempat menulis file `.lynx`. File `.flp` itu sendiri tidak pernah ditulis.

## Padanannya

| Di FL Studio | Di LYNX SHARE |
|---|---|
| Playlist, Channel Rack, Piano Roll, Mixer | Nama sama, di `F5`, `F6`, `F7` dan `F9` |
| Pemilih plugin | `F8` |
| Browser | `Alt+F8` |
| Mode Pattern / Song | `L` |
| Urungkan | `Ctrl+Z` mengurungkan, atau mengulangi setelah sebuah urungkan. `Ctrl+Alt+Z` mengurungkan mundur |
| Simpan versi baru | `Ctrl+Alt+S` |
| Ekspor | `Ctrl+R`, MP3 di `Ctrl+Shift+R`, MIDI di `Ctrl+Shift+M` |
| Insert, send, dan sidechain Mixer | Sama, dengan 10 slot efek per insert |
| Fruity Wrapper | Plugin pihak ketiga di-host langsung, lihat [Plugin pihak ketiga](/wiki/plugins/) |
| Generator dan efek bawaan FL | Dimainkan di engine yang dibuat ulang LYNX SHARE, lihat di bawah |
| File proyek `.flp` | File proyek `.lynx` |

`Bantuan › Pintasan` mencantumkan semua tombol.

## Yang ikut diimpor

- Channel, sample, pattern, not slide dan porta, semua aransemen, marker, perubahan birama, otomasi, dan tautan kontroler.
- Mixer: routing, level, sidechain, EQ track, dan efek bawaan beserta pengaturannya yang tersimpan.
- Plugin pihak ketiga VST3, VST2 dan CLAP di dalam Fruity Wrapper: diganti dengan plugin yang sama yang terpasang di komputermu, lengkap dengan state-nya yang tersimpan.

## Plugin bawaan FL Studio

Generator dan efek bawaan FL Studio tidak dimuat. LYNX SHARE memainkannya di engine yang dibuat ulang, dan masing-masing punya tingkat kualitas:

| Tingkat | Artinya |
|---|---|
| Exact | Identik bit demi bit dengan aslinya pada setiap state yang dites |
| Near-exact | Identik bit demi bit pada sebagian besar state, sisanya berbeda di bawah sekitar -100 dB |
| Close | Algoritma yang sama, dalam jarak -60 dB dari aslinya pada hampir semua state yang dites |
| Approximate | Pengaturan tersimpan dipetakan ke synth atau efek bawaan LYNX SHARE |
| Placeholder | State dipertahankan dan plugin senyap atau hanya meneruskan audio |

Tingkat setiap engine, dan apa yang masih kurang dari masing-masing, ada di [PLUGINS.md](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/PLUGINS.md). Lihat juga [Instrumen dan efek bawaan](/wiki/built-in/).

## Yang membutuhkan instalasi FL Studio milikmu

Membuka `.flp` bisa dilakukan tanpa FL Studio terpasang. Instalasi di komputer yang sama menambah dua hal:

- Sample pabrikan FL Studio dan database plugin-nya, sehingga sample dan plugin ditemukan tanpa pemindaian.
- Gelombang, sample, impulse, dan tabel bawaan yang dibutuhkan sebagian engine (FL Keys, Soundgoodizer, Maximus, dan yang lain yang bertanda belati di PLUGINS.md). Semuanya dibaca dari instalasimu saat dijalankan dan tidak disertakan dengan LYNX SHARE.

Di Linux, LYNX SHARE mencari FL Studio Windows di bawah Wine. Di macOS, app bundle-nya dicari, tetapi ini belum dites.

## Ekspor kembali ke .flp

1. Pilih `File › Ekspor › Proyek FL Studio...`.
2. Pilih `Proyek FL Studio (.flp)` atau `Paket loop zip (.zip, dengan semua sampel)`.
3. Pilih apa yang terjadi pada hal-hal yang tidak dimiliki FL Studio: render ke audio (suaranya sama), atau pakai plugin FL terdekat (tetap bisa diedit, suaranya bisa berbeda, dan instrumen yang hanya ada di LYNX dilewati).

File itu adalah proyek FL Studio 2026. Sampler, clip audio, generator dan efek bawaan FL, plugin pihak ketiga (di dalam Fruity Wrapper), pattern, Playlist, Mixer, dan otomasi ikut terbawa beserta pengaturannya. Sebuah laporan mencantumkan apa yang terbawa persis, dan apa yang di-render, diganti, atau dilewati.

## FL Studio Mobile (.flm)

`File › Buka...` juga menerima proyek `.flm`. Impornya parsial.

- Diimpor: tempo, birama, satu insert mixer per channel FLM, satu track Playlist per lane, pattern (setiap putaran loop sebagai clip), clip audio dengan offset dan pitch, drum kit, DW Sampler dan program `.dwp` serta Slicer (di engine FLM sendiri), instrumen MiniSynth, Transistor Bass, 3x Osc, GMSynth, dan SuperSaw, efek FLM (sebagai efek Pocket), otomasi level, pan, mute, solo, pitch bend, send, dan parameter instrumen, serta chord set dan key locker.
- Tidak diimpor: instrumen FLM lainnya, yang dimainkan dengan synth bawaan.
- Sample dicari di sebelah `.flm` dan di salinan sumber daya FLM milik FL Studio. `.dwp` yang tidak ditemukan dimainkan dengan pengganti.

## Baik diketahui

- Paket FLEX dienkripsi dan tidak pernah dibuka. Channel FLEX mendapat patch synth yang dipilih berdasarkan nama preset-nya.
- Modulator dilewati dalam ekspor `.flp`, dan laporan ekspor menyebutkannya.
- Laporan impor mencantumkan apa yang diperkirakan oleh synth bawaan, apa yang tidak punya padanan, plugin mana yang ditemukan atau belum terpasang, efek mana yang dilewati atau hanya sebagian direproduksi, dan berapa banyak sample yang hilang.
- Efek pihak ketiga yang belum terpasang diganti dengan efek LYNX SHARE yang berperan sama. Aslinya tetap ada di proyek.

## Terkait

- [Instrumen dan efek bawaan](/wiki/built-in/)
- [Plugin pihak ketiga](/wiki/plugins/)
- [Format file](/wiki/file-formats/)
- [Pintasan](/wiki/shortcuts/)
- [Yang hanya dilakukan LYNX SHARE dengan pengaturan FL](/why/#fl-settings)
