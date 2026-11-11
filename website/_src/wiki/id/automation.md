---
source: b96330a448
---
![Editor otomasi dengan kurva yang naik selama enam bar](/assets/img/wiki/automation.webp)

Clip otomasi adalah kurva di [Playlist](/wiki/playlist/) yang menggerakkan satu kontrol selama lagu diputar: volume channel, fader mixer, parameter plugin, tempo. Clip itu punya jendela editornya sendiri, yaitu Editor otomasi.

Ada tiga cara lain untuk menggerakkan kontrol. Event adalah nilai yang tersimpan di dalam pattern. Tautan membuat knob MIDI atau kontroler internal menggerakkan kontrol itu. Modulator menggoyang knob di sekitar nilainya sendiri.

## Mengotomasi sebuah knob

1. Pindahkan playhead ke tempat otomasi harus dimulai, atau pilih rentang waktu di Playlist.
2. Klik kanan knob atau fader, lalu pilih `Buat clip otomasi`.
3. Clip mendarat di track Playlist kosong pertama dan Editor otomasi terbuka. Kurva dimulai sebagai garis datar pada nilai kontrol saat ini.
4. Klik di editor untuk menambah titik, lalu seret ke tempatnya.
5. Tahan `Alt` dan seret sebuah segmen ke atas atau ke bawah untuk melengkungkannya.
6. Putar lagu dalam mode Song. Kontrol mengikuti kurva.

Cara lain untuk mendapatkan clip yang sama:

- `Tambah › Otomasi untuk parameter yang terakhir diubah` membuat clip untuk kontrol yang terakhir kamu gerakkan.
- Tampilan kontrol yang terakhir diubah, dan `Alat › Terakhir diubah`, membuka menu yang sama dengan klik kanan pada kontrolnya.
- Untuk plugin pihak ketiga, buka menu channel di Channel Rack atau menu slot efek di Mixer, lalu pakai `Otomasikan parameter plugin`. `Otomasikan kontroler MIDI (CC, aftertouch, pitch bend)` ada di sebelahnya.

Untuk membuka editor lagi, klik dua kali clip, atau klik kanan lalu pilih `Edit titik...`. `Edit clip otomasi...` di menu kontrol itu sendiri melakukan hal yang sama.

## Editor otomasi

| Perintah | Tombol | Fungsinya |
|---|---|---|
| Titik baru | Klik atau klik kanan, lalu seret | Menambah titik dan menggerakkannya selama tombol ditahan |
| Menggeser titik | Seret | `Shift` mempertahankan waktunya, `Ctrl` mempertahankan nilainya, `Alt` mengabaikan snap |
| Melengkungkan segmen | `Alt`+seret ke atas atau ke bawah | Mengubah tegangan segmen di bawah mouse |
| Handle tegangan | Seret ke atas atau ke bawah | Melengkungkan segmen. `Ctrl` lebih halus, klik kanan meluruskannya |
| Titik pada kurva | `Shift`+klik kanan | Menambah titik pada garis |
| Menu titik | Klik kanan sebuah titik | `Ketik nilai...`, `Salin nilai`, `Tempel nilai`, `Hapus titik`, dan `Seluruh kurva` untuk menskalakan, membalik, atau meluruskan semuanya |
| Zoom | `Ctrl`+roda | Memperbesar atau memperkecil tampilan |
| Gulir | Seret dengan tombol tengah | Menggeser tampilan |

## Mode LFO

![Editor otomasi dalam mode LFO, dengan gelombang segitiga dan knob SPD, TNS, SKEW, PW dan AMNT](/assets/img/wiki/automation-lfo.webp)

Centang `LFO` di bagian atas editor. Sebuah bentuk gelombang kini membentuk nilai clip, dan titik yang kamu gambar menentukan amplitudo LFO, bukan nilainya langsung.

| Pengaturan | Fungsinya |
|---|---|
| `SPD` | Panjang satu siklus, dalam beat |
| `TNS` | Tegangan gelombang |
| `SKEW` | Skew gelombang |
| `PW` | Pulse width |
| `AMNT` | Seberapa jauh LFO menggerakkan nilai |
| `MUL` | Aktif: LFO dikalikan dengan level envelope. Mati: LFO ditambahkan |

## Event di dalam pattern

![Editor event dengan event volume yang digambar sebagai batang](/assets/img/wiki/eventeditor.webp)

Event milik sebuah pattern dan diputar di mana pun pattern itu diputar. Klik kanan sebuah kontrol dan pilih `Edit event di pattern` untuk membuka Editor event baginya.

- `Gambar` mengatur nilai saat kamu menyeret, dan seretan klik kanan menghapus. `Garis` menggambar tanjakan lurus. `Pilih` menandai rentang waktu untuk alat di bagian bawah.
- `Snap` berkisar dari 1/2 step sampai 1 bar.
- `Tangga nada`, `Offset` dan `Acak` mengubah event dalam pilihan, atau di seluruh pattern.
- `LFO` menulis gelombang sinus, segitiga, atau persegi sepanjang 1 beat sampai 4 bar.
- `Bersihkan` menghapus event.
- `Ke otomasi...` mengubah event menjadi clip otomasi dan menghapusnya dari pattern.
- `File` membuka dan menyimpan event sebagai file `.fsc`, dan mengimpor file MIDI.

`Inisialisasi song dengan posisi ini`, di menu klik kanan yang sama, menyimpan nilai tempat kontrol di-reset setiap kali lagu dimulai.

## Merekam gerakanmu

Nyalakan perekaman dengan `Otomasi` dicentang di filter rekaman, tekan play, lalu gerakkan kontrolnya. Dalam mode Song, gerakan itu menjadi clip otomasi. Dalam mode Pattern, gerakan itu menjadi event di pattern saat ini. Lihat [Merekam](/wiki/recording/).

## Tautan dan modulator

- `Tautkan ke kontroler MIDI (gerakkan knob pada kontroler)` dibahas di [Controller MIDI](/wiki/midi-controllers/).
- `Tautkan ke kontroler internal` membuat Envelope, Formula, atau Peak Controller di proyek menggerakkan kontrol itu. Tambahkan salah satunya lebih dulu.
- `Modulasikan` memasang modulator, yaitu LFO atau sumber acak yang menggerakkan knob LYNX SHARE di sekitar nilainya sendiri. `Alat › Modulator...` mencantumkannya.

## Jalur otomasi

Jalur mati secara bawaan. Aktifkan `Jalur otomasi` di menu Playlist. Setiap header track lalu punya tombol yang menambah jalur di bawah track, dan menu kontrol mendapat `Otomasikan di jalur`. Di dalam jalur, klik untuk menambah titik atau seret untuk memindahkannya, klik kanan untuk menghapus titik, dan `Alt`+seret untuk melengkungkan garis. Menu jalur punya `Ke klip otomasi`.

## Baik diketahui

- Clip baru dimulai di seleksi waktu kalau ada, kalau tidak di playhead dalam mode Song. Matikan `Buat klip otomasi di posisi pemutaran` di `Opsi › Pengaturan umum...` untuk mulai dari awal lagu.
- Beberapa clip pada satu kontrol masing-masing diputar di tempatnya sendiri. Sebelum clip pertamanya, kontrol mempertahankan nilainya sendiri.
- Di awal sebuah clip, kontrol mencapai nilai baru dalam sekitar 8 ms. FL Studio meluncur ke sana, jadi proyek hasil impor bisa sedikit berbeda di titik-titik itu.
- Di tempat clip pada kontrol yang sama bertumpuk, clip yang dipilih LYNX SHARE bisa berbeda dari yang dipilih FL Studio.
- Modulator hanya bekerja pada knob LYNX SHARE sendiri, dan tidak ikut saat kamu mengekspor ke `.flp`.

## Terkait

- [Playlist](/wiki/playlist/)
- [Merekam](/wiki/recording/)
- [Controller MIDI](/wiki/midi-controllers/)
- [Mixer](/wiki/mixer/)
- [Pintasan](/wiki/shortcuts/)
