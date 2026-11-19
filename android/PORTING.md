# Porting Parser Desktop (.flp/.als/.flm) ke Android

## Ringkasan

Parser desktop (`FlpImport`, `AlsImport`, `FlmImport`) terintegrasi sangat dalam dengan engine desktop:

| Dependensi | File | Keterkaitan |
|------------|------|-------------|
| `Song` data model | `src/core/Song.h` (2916 baris) | Struktur inti proyek (pattern, mixer, meter, kanal) |
| Import .flp | `src/core/FlpImport.{h,cpp}` (229KB + 9KB) | OLE compound file parsing, sample resolution |
| Import .als | `src/core/AlsImport.{h,cpp}` (144KB + 5KB) | gzip XML, Live set parsing, device translation |
| Import .flm | `src/core/FlmImport.{h,cpp}` (161KB + 6KB) | FL Studio Mobile binary parsing |
| 30+ generator/effect | `src/core/*.cpp` | Sintesis bawaan FL, efek, drum, sampler |
| JUCE types | `juce::String`, `juce::Array`, `juce::File` | Digunakan throughout parser |
| File system | `std::filesystem`, callbacks `fileExists`, `readFile` | Untuk mencari samples |

Porting penuh memerlukan rewriting engine audio Android untuk menangani:
- `Song` dengan 501 mixer tracks, PPQ 960, meter segala jenis
- Semua generator bawaan (MiniSynth, FPC, DirectWave, dll.)
- Semua efek bawaan (EQ, reverb, delay, grossbeat, dll.)
- Sample resolution dengan path roots FL Studio

## Strategi Porting (Langkah demi Langkah)

### Fase 1: Format Native `.lynx` (Selesai ✅)

Engine Android sudah memiliki `ProjectData` dan `project_file.{h,cpp}` untuk save/load format native `.lynx`. Ini adalah format terpendek yang bisa digunakan segera.

### Fase 2: Stub Importer (Selesai ✅)

`importer_stub.{h,cpp}` menyediakan:
- Deteksi magic bytes untuk .flp (OLE), .als (gzip), .flm
- Error message yang jelas saat pengguna mencoba impor
- API siap dipanggil: `importFlpStub`, `importAlsStub`, `importFlmStub`

### Fase 3: Port `Song` Model ke Android (Selesai ✅)

Copy `src/core/Song.h` dan struktur terkait ke `android/app/src/main/cpp/`:
- `song_model.h` — data model proyek (disederhanakan untuk Android)
- `Meter`, `Pattern`, `MixerTrack`, `Clip`, `Note`, `ChannelDef`, `Automation`
- Tanpa JUCE dependencies
- Integrasi dengan `project_file.{h,cpp}` untuk save/load
- Update `audio_engine.{h,cpp}` dan `jni_bridge.cpp` untuk menggunakan `Song` model

### Fase 4: Port Parser `.flp` (OLE Compound File) — **Dibatalkan**

Fokus pada format native `.lynx` (Fase 1-3). Parser .flp membutuhkan OLE compound file reader + mapping 30+ generator/efek FL Studio, estimasi 6-10 minggu dengan ROI rendah. Format native `.lynx` sudah lengkap untuk workflow Android.

### Fase 5: Port Parser `.als` (Ableton Live) ⏸️ **Belum Dimulai**

1. Implementasi gzip decompression (zlib/miniz)
2. Port XML parser untuk Live set
3. Port `AlsImport.{h,cpp}`
4. Map Live device → LYNX instrument/effect

Estimasi: 3-4 minggu | Kompleksitas: Tinggi

### Fase 6: Port Parser `.flm` (FL Studio Mobile) (Selesai ✅)

1. Port `flm/FlmFormat.{h,cpp}` → `flm_parser.h/cpp` (binary chunk reader)
2. Port `FlmImport.{h,cpp}` → `flm_importer.h/cpp` (mapping ke Song model)
3. Map FLM rack/generator → SimpleSynth, DrumSynth, DelayEffect, ReverbEffect Android

Estimasi: 2-3 minggu | Kompleksitas: Sedang | Status: **Selesai**

## API Importer (Siap Digunakan)

```cpp
// Di importer_stub.h
struct ImportResult { bool ok; std::string error; };
bool isFlpData(const uint8_t* data, size_t size);
bool isAlsData(const uint8_t* data, size_t size);
bool isFlmData(const uint8_t* data, size_t size);
ImportResult importFlpStub(const vector<uint8_t>& bytes);
ImportResult importAlsStub(const vector<uint8_t>& bytes);
ImportResult importFlmStub(const vector<uint8_t>& bytes);
```

Ketika parser desktop diport, ganti isi `importFlpStub`/`importAlsStub`/`importFlmStub` dengan panggilan ke fungsi import asli dan konversi `Song` → `ProjectData`.

## Estimasi Usaha

| Fase | Estimasi | Kompleksitas | Status |
|------|----------|--------------|--------|
| Fase 1: Format `.lynx` | Selesai | Sedang | ✅ Done |
| Fase 2: Stub importer | Selesai | Sedang | ✅ Done |
| Fase 3: Port `Song` model | 1-2 minggu | Sedang | ✅ Done |
| Fase 4: Port `.flp` | 4-6 minggu | Tinggi | Dibatalkan (fokus native) |
| Fase 5: Port `.als` | 3-4 minggu | Tinggi | Belum dimulai |
| Fase 6: Port `.flm` | 2-3 minggu | Sedang | ✅ Done |
| **Total estimasi sisa** | **3-4 minggu** | — | **Fase 5 (`.als`) saja** |

Saran: Mulai dengan format native `.lynx` untuk workflow Android, lalu port parser desktop secara bertahap.

## File Terkait

| File | Fungsi |
|------|--------|
| `android/app/src/main/cpp/project_file.{h,cpp}` | Format `.lynx` save/load (Fase 1) |
| `android/app/src/main/cpp/importer_stub.{h,cpp}` | Deteksi + error stub (Fase 2) |
| `android/app/src/main/cpp/song_model.h` | Song model Android (Fase 3) |
| `android/app/src/main/cpp/flm_parser.{h,cpp}` | FLM binary chunk parser (Fase 6) |
| `android/app/src/main/cpp/flm_importer.{h,cpp}` | FLM → Song model converter (Fase 6) |
| `android/app/src/main/cpp/audio_engine.{h,cpp}` | Engine audio + Song integration |
| `android/app/src/main/cpp/jni_bridge.cpp` | JNI bridge + FLM import API |
| `android/app/src/main/cpp/CMakeLists.txt` | Build config untuk semua file engine |
| `android/PORTING.md` | Dokumen strategi dan milestone porting ini |

## Catatan

Parser desktop (`FlpImport`, `AlsImport`, `FlmImport`) terintegrasi sangat dalam dengan engine desktop (30+ generator, efek bawaan, JUCE types, file system callbacks). Porting penuh memerlukan rewriting engine audio Android. Saran: gunakan format native `.lynx` untuk workflow Android dulu, baru port parser desktop secara bertahap.
