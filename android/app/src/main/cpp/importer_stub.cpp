#include "importer_stub.h"

#include <cstring>

namespace {

constexpr char kFlpMagic[] = "FLP ";  // FL Studio project (OLE compound file)
constexpr char kAlsHeader[] = "Ableton";  // Live set (gzip XML)
constexpr char kFlmMagic[] = "FLM";  // FL Studio Mobile

bool startsWith(const std::vector<std::uint8_t>& data, const char* prefix) {
    std::size_t n = std::strlen(prefix);
    if (data.size() < n) return false;
    return std::memcmp(data.data(), prefix, n) == 0;
}

} // namespace

bool isFlpData(const std::uint8_t* data, std::size_t size) {
    // FL Studio projects use OLE compound file; magic "FTHR" or similar.
    // Simplest heuristic: check first bytes.
    if (size < 4) return false;
    return std::memcmp(data, "\xD0\xCF\x11\xE0", 4) == 0;  // OLE compound file signature
}

bool isAlsData(const std::uint8_t* data, std::size_t size) {
    if (size < 8) return false;
    // Ableton sets are gzip'd XML starting with Ableton header in decompressed form.
    // Check gzip magic + "Ableton" after decompress (not done here).
    return data[0] == 0x1F && data[1] == 0x8B;  // gzip magic
}

bool isFlmData(const std::uint8_t* data, std::size_t size) {
    if (size < 4) return false;
    return data[0] == 'F' && data[1] == 'L' && data[2] == 'M';
}

ImportResult importFlpStub(const std::vector<std::uint8_t>& /* fileBytes */) {
    ImportResult r;
    r.ok = false;
    r.error = ".flp import belum diport ke Android. Parser desktop (FlpImport) butuh stack engine penuh (Song, Scheduler, Instruments, JUCE types). Port terlebih dahulu atau gunakan format native .lynx.";
    return r;
}

ImportResult importAlsStub(const std::vector<std::uint8_t>& /* fileBytes */) {
    ImportResult r;
    r.ok = false;
    r.error = ".als import belum diport ke Android. Parser desktop (AlsImport) butuh stack engine penuh. Port terlebih dahulu atau gunakan format native .lynx.";
    return r;
}

ImportResult importFlmStub(const std::vector<std::uint8_t>& /* fileBytes */) {
    ImportResult r;
    r.ok = false;
    r.error = ".flm import belum diport ke Android. Parser desktop (FlmImport) butuh stack engine penuh. Port terlebih dahulu atau gunakan format native .lynx.";
    return r;
}
