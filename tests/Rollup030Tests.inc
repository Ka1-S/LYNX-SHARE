// Rollup 0.1.30 (round 190il): what two of the merged branches do together.
// - formats190ii found that FL's shipped version-36 DirectWave programs store the sample format record (0x1F7) in 36
//   bytes (no bit depth), and that DirectWave reads every record as read (min (size, record)) over a record of zeros
//   (the bit depth 32). It completed the records in its own importer only; dwave190hz's editor ("Open program") and old
//   .flp projects went through readZone, which skipped the short record and with it the loop. readZone now reads short
//   records the way DirectWave does, for every path.

#include <cstring>

#include "core/DirectWave.h"
#include "core/DirectWaveState.h"
#include "core/SamplerImport.h"

namespace rollup030
{
struct DwBytes
{
    std::vector<std::uint8_t> b;
    void u8 (unsigned v) { b.push_back ((std::uint8_t) v); }
    void u32 (std::uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back ((std::uint8_t) (v >> (8 * i))); }
    void u64 (std::uint64_t v) { for (int i = 0; i < 8; ++i) b.push_back ((std::uint8_t) (v >> (8 * i))); }
    void f32 (float f) { std::uint32_t v; std::memcpy (&v, &f, 4); u32 (v); }
    void chunk (std::uint32_t id, const std::vector<std::uint8_t>& body)
    {
        u32 (id);
        u64 (body.size());
        b.insert (b.end(), body.begin(), body.end());
    }
};

// a sample format record (0x1F7) of `size` bytes: 1000 frames, mono, 44.1 kHz, the loop, offset 0, bits 24
inline std::vector<std::uint8_t> formatRecord (std::size_t size, int loopMode, int loopA, int loopB)
{
    DwBytes f;
    f.u64 (1000); f.u32 (1); f.u32 (16); f.f32 (44100.0f);
    f.u32 ((std::uint32_t) loopMode); f.u32 ((std::uint32_t) loopA); f.u32 ((std::uint32_t) loopB); f.u32 (0); f.u32 (24);
    f.b.resize (size);
    return f.b;
}

// one zone: keys 44…46, root 45, the sample format record as given
inline std::vector<std::uint8_t> zoneBody (const std::vector<std::uint8_t>& format)
{
    DwBytes header;
    header.u8 (45); header.u8 (44); header.u8 (46); header.u8 (0); header.u8 (127);
    header.u8 (0); header.u8 (0); header.u8 (204); header.u8 (0);
    header.f32 (1.0f); header.f32 (0.5f); header.u32 (0); header.u32 (512);
    DwBytes zone;
    zone.chunk (0x1F4, header.b);
    zone.chunk (0x1F5, { 'z' });
    const std::string path = "%FLStudioData%\\Patches\\Packs\\Piano\\Piano 2\\a.wav";
    zone.chunk (0x1F6, std::vector<std::uint8_t> (path.begin(), path.end()));
    zone.chunk (0x1F7, format);
    zone.chunk (4, {});
    return zone.b;
}

// a DirectWave state of `version` (as an .flp or a program file after its magic holds it): one program, one zone
inline std::vector<std::uint8_t> stateWith (std::uint32_t version, const std::vector<std::uint8_t>& zone)
{
    DwBytes p64;
    p64.u32 (0); p64.u8 (0); p64.u8 (0); p64.f32 (1.0f); p64.f32 (0.0f);
    for (int i = 0; i < 20; ++i)
        p64.u8 (0);
    DwBytes prog;
    prog.chunk (0x64, p64.b);
    prog.chunk (0x66, { 'P', 'i', 'a', 'n', 'o' });
    prog.chunk (3, zone);
    prog.chunk (2, {});
    DwBytes s;
    s.u32 (version);
    s.chunk (6, std::vector<std::uint8_t> (16, 0));
    s.chunk (1, prog.b);
    return s.b;
}
} // namespace rollup030

TEST (rollup030_directwave_short_format_record_keeps_its_loop)
{
    using namespace rollup030;
    // FL's version-36 record: 36 bytes, no bit depth → the loop is read, bits = 32 (DirectWave's record starts with 32)
    {
        const auto body = zoneBody (formatRecord (36, 2, 300, 800));
        const auto z = lynx::dw::parseZone (body.data(), body.size());
        CHECK (z.loopMode == 2 && z.loopA == 300 && z.loopB == 800 && z.frames == 1000 && z.rate == 44100.0f);
        CHECK (z.bits == 32 && z.offset == 0 && z.root == 45 && z.lowKey == 44 && z.highKey == 46);
    }
    // the full 40-byte record keeps its own bit depth
    {
        const auto body = zoneBody (formatRecord (40, 1, 10, 900));
        const auto z = lynx::dw::parseZone (body.data(), body.size());
        CHECK (z.loopMode == 1 && z.loopA == 10 && z.loopB == 900 && z.bits == 24);
    }
    // shorter still: what is there is read, the rest is DirectWave's zeros (no loop end, offset 0), bits 32
    {
        const auto body = zoneBody (formatRecord (28, 2, 300, 800));
        const auto z = lynx::dw::parseZone (body.data(), body.size());
        CHECK (z.loopMode == 2 && z.loopA == 300 && z.loopB == 0 && z.offset == 0 && z.bits == 32 && z.frames == 1000);
    }
}

TEST (rollup030_directwave_v36_state_loops_on_every_path)
{
    using namespace rollup030;
    const auto state = stateWith (36, zoneBody (formatRecord (36, 2, 300, 800)));
    const auto loopKept = [] (const lynx::dw::Zone& z) { return z.loopMode == 2 && z.loopA == 300 && z.loopB == 800 && z.bits == 32; };

    // an old .flp project's DirectWave state, as stored (no importer in between)
    const auto st = lynx::dw::parseState (state);
    CHECK (st && lynx::dw::playedProgram (*st) != nullptr);
    if (st && lynx::dw::playedProgram (*st) != nullptr)
        CHECK (loopKept (lynx::dw::playedProgram (*st)->zones.at (0)));

    // dwave190hz's editor, "Open program" on a version-36 .dwp
    auto file = std::vector<std::uint8_t> { 'D', 'w', 'P', 'r' };
    file.insert (file.end(), state.begin(), state.end());
    const auto opened = lynx::dw::readProgramFile (file);
    CHECK (opened && ! opened->zones.empty());
    if (opened && ! opened->zones.empty())
        CHECK (loopKept (opened->zones[0]));

    // formats190ii's importer: the state it keeps is the file's own, and it reads the same
    const auto imported = lynx::msimport::stateOfFile (file);
    CHECK (imported && *imported == state);
    if (imported)
        if (const auto again = lynx::dw::parseState (*imported); again && lynx::dw::playedProgram (*again) != nullptr)
            CHECK (loopKept (lynx::dw::playedProgram (*again)->zones.at (0)));
}
