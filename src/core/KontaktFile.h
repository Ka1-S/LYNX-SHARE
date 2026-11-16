#pragma once

// Kontakt instruments (.nki) (round 190ii, issue #149): what LYNX can say about one. Kontakt's library format is
// closed and protected libraries are encrypted; LYNX never decrypts or works around that protection. The samples of
// unprotected material in NCW (Kontakt's lossless compressed WAV) open as audio (core/Ncw.h). Reading the zone map of
// unencrypted instruments (Kontakt 1 / 2 XML, the NI container of Kontakt 4.2 and later) is not done yet: the file is
// recognised and the import dialog says what it is (docs/analise/REA_rodada190ii.md §3, from the public descriptions
// of the format in ConvertWithMoss, LGPL-3.0).

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/SamplerImport.h"

namespace lynx::msimport::kontakt
{
enum class Kind
{
    unknown,
    kontakt1,         // zlib XML (NiSS)
    kontakt2,         // Kontakt 1.5 … 4.1 header + zlib XML (K2)
    kontakt42,        // the 4.2 header with FastLZ preset data
    container,        // the NI container ("hsin": Kontakt 4.2.2 … 8)
    monolith          // a Kontakt 5 monolith (samples inside)
};
Kind detect (const std::uint8_t* data, std::size_t size);
// The container holds an encryption or authorization (licensed library) item
bool protectedContainer (const std::uint8_t* data, std::size_t size);

std::optional<Program> readInstrument (const std::string& path, const Access& access, Report& report);
} // namespace lynx::msimport::kontakt
