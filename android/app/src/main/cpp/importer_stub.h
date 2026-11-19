#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// ImporterStub — placeholder untuk impor .flp / .als / .flm dari desktop LYNX-SHARE.
//
// Parser desktop (FlpImport, AlsImport, FlmImport) terintegrasi sangat dalam
// dengan engine desktop (Song, Scheduler, Instruments, JUCE types, file-system
// callbacks). Porting penuh ke Android memerlukan stack engine desktop yang
// lengkap dan belum diimplementasi di sini.
//
// File ini menyediakan:
//   - Deteksi magic bytes masing-masing format
//   - Stub yang mengembalikan error dengan pesan yang jelas
//
// Saat parser desktop diport, ganti isi fungsi di bawah dengan panggilan ke
// importFlp / importAls / importFlm dan konversi Song → ProjectData.

struct ImportResult {
    bool ok = false;
    std::string error;
};

bool isFlpData(const std::uint8_t* data, std::size_t size);
bool isAlsData(const std::uint8_t* data, std::size_t size);
bool isFlmData(const std::uint8_t* data, std::size_t size);

// Import .flp bytes → ProjectData. Saat ini hanya stub.
ImportResult importFlpStub(const std::vector<std::uint8_t>& fileBytes);

// Import .als bytes → ProjectData. Saat ini hanya stub.
ImportResult importAlsStub(const std::vector<std::uint8_t>& fileBytes);

// Import .flm bytes → ProjectData. Saat ini hanya stub.
ImportResult importFlmStub(const std::vector<std::uint8_t>& fileBytes);
