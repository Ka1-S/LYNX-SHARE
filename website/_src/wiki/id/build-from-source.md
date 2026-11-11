---
source: edc71061e6
---
LYNX SHARE ditulis dalam C++20 dengan JUCE 8 dan di-build dengan CMake. Source-nya ada di [github.com/Ka1-S/LYNX-SHARE](https://github.com/Ka1-S/LYNX-SHARE) dengan lisensi GNU Affero General Public License v3.0. File rilis di-build dari source yang di-tag oleh GitHub Actions, jadi halaman ini hanya kamu perlukan kalau ingin mengubah kode atau membuild sendiri.

## Build di Linux

1. Instal dependensi yang tercantum di bawah.
2. Ambil source dengan `git clone https://github.com/Ka1-S/LYNX-SHARE.git lynx` lalu masuk ke folder `lynx`.
3. Konfigurasikan dengan `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release`.
4. Build dengan `cmake --build build`.

Dependensi di Ubuntu 24.04:

```
sudo apt install build-essential cmake ninja-build pkg-config libasound2-dev libjack-jackd2-dev libx11-dev \
  libxrandr-dev libxinerama-dev libxcursor-dev libxext-dev libxcomposite-dev libfreetype-dev libfontconfig1-dev libgl-dev
```

## Build di Windows dengan Visual Studio

Membutuhkan Visual Studio 2022 Build Tools dengan "Desktop development with C++", CMake 3.22 atau lebih baru, dan Git.

```
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

## Build untuk Windows dari WSL atau Linux

Beginilah proyek ini sendiri dikembangkan: program Windows di-cross-compile dengan Zig, tanpa Visual Studio.

```
./scripts/build-windows-zig.sh
```

Hasilnya adalah `build-win/LYNX.exe`.

| Kebutuhan | Versi | Lokasi bawaan |
|---|---|---|
| Zig | 0.14.1 | `~/opt/zig-x86_64-linux-0.14.1` |
| JUCE | 8.0.4 | `~/opt/juce-8.0.4` |

JUCE bisa diambil dengan:

```
git clone --depth 1 -b 8.0.4 https://github.com/juce-framework/JUCE ~/opt/juce-8.0.4
```

Atur variabel lingkungan `ZIG` dan `JUCE_DIR` kalau lokasimu berbeda.

## Build di macOS

Membutuhkan Xcode command line tools (`xcode-select --install`), CMake dan Ninja, misalnya dari `brew install cmake ninja`.

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build
```

`CMAKE_OSX_ARCHITECTURES="arm64;x86_64"` menghasilkan aplikasi universal. Hilangkan opsi itu untuk membuild hanya bagi Mac yang sedang kamu pakai.

## Menjalankan tes inti

Inti program (model, sequencer, instrumen, efek, dan impor `.flp`) adalah C++ biasa dan dites tanpa JUCE, dengan compiler C++20 apa pun.

```
./scripts/build-local.sh ["folder with .flp files"]
```

Hasil akhirnya harus `N checks, 0 failures`. Kalau diberi folder `Data` dari instalasi FL Studio 2026, skrip ini juga mem-parse proyek `.flp` yang disertakan FL Studio. Skrip ini juga membuild dua alat kecil: `build-local/flpdump`, yang membuang isi sebuah `.flp`, dan `build-local/renderwav`.

Setelah build, program bisa memeriksa dirinya sendiri tanpa perangkat audio:

```
LYNX.exe --selftest report.txt
```

Laporannya diakhiri dengan `result=OK`. Opsi lainnya ada di halaman [Command line](/wiki/command-line/).

## Struktur source

| Folder | Isinya |
|---|---|
| `src/core/` | Model, sequencer, instrumen, efek, dan impor `.flp`. C++ biasa, bisa dites tanpa JUCE |
| `src/flp/` | Pembaca format `.flp` |
| `src/engine/` | Engine audio: perangkat, graph, sample, plugin, file proyek |
| `src/ui/` | Antarmuka: Channel Rack, Piano Roll, Playlist, Mixer, jendela |
| `third_party/` | CLAP dan decoder WavPack |
| `tests/` | Tes inti |
| `tools/` | `flpdump`, `renderwav`, `comparewav`, `make_logo` |
| `docs/` | Arsitektur, status, roadmap, dan analisis perilaku FL Studio |

## Baik diketahui

- JUCE 8 tidak mendukung MinGW secara resmi. Skrip build Zig mengatasi perbedaannya. Perbaikannya ada di `cmake/mingw/` dan `src/platform/MingwFixes.cpp`.
- Jalankan hanya satu `build-windows-zig.sh` pada satu waktu. Dua build Zig sekaligus bisa menggantung. Untuk build paralel di worktree terpisah, beri masing-masing cache sendiri dengan `ZIG_GLOBAL_CACHE_DIR` dan `ZIG_LOCAL_CACHE_DIR`.
- Beberapa engine membaca suara dan tabel bawaan dari instalasi FL Studio milikmu sendiri. Tidak ada apa pun dari FL Studio di repositori.
- Sebagian dokumentasi di `docs/` masih berbahasa Portugis. Sedang diterjemahkan.
- Kalau kamu mendistribusikan versi yang dimodifikasi, termasuk sebagai layanan jaringan, lisensi mewajibkan kamu menerbitkan source-nya dengan lisensi yang sama.

## Terkait

- [Lingkungan pengembangan lengkap (SETUP_DEV.md)](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/SETUP_DEV.md)
- [LYNX SHARE di Linux (LINUX.md)](https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/LINUX.md)
- [Repositori dan README-nya](https://github.com/Ka1-S/LYNX-SHARE)
- [Command line](/wiki/command-line/)
- [Format file](/wiki/file-formats/)
