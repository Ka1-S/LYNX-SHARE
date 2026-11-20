#include "core/SamplerImport.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

#include "core/DirectWave.h"
#include "core/GigFile.h"
#include "core/KontaktFile.h"

namespace lynx::msimport
{
namespace
{
// ------------------------------------------------------------------------------------------------ writing chunks
struct Out
{
    std::vector<std::uint8_t> b;
    void u8 (std::uint8_t v) { b.push_back (v); }
    void u16 (std::uint16_t v) { put (&v, 2); }
    void u32 (std::uint32_t v) { put (&v, 4); }
    void i32 (std::int32_t v) { put (&v, 4); }
    void i64 (std::int64_t v) { put (&v, 8); }
    void f32 (float v) { put (&v, 4); }
    void put (const void* p, std::size_t n) { const auto* c = (const std::uint8_t*) p; b.insert (b.end(), c, c + n); }
    void str (const std::string& s) { put (s.data(), s.size()); }
    // chunk: u32 id, u64 size, body
    std::size_t open (std::uint32_t id) { u32 (id); const auto at = b.size(); i64 (0); return at; }
    void close (std::size_t at) { const auto size = (std::uint64_t) (b.size() - at - 8); std::memcpy (b.data() + at, &size, 8); }
    void chunk (std::uint32_t id, const std::vector<std::uint8_t>& body) { const auto at = open (id); put (body.data(), body.size()); close (at); }
    void chunk (std::uint32_t id, const std::string& body) { const auto at = open (id); str (body); close (at); }
};

std::vector<std::uint8_t> hex (const char* h)
{
    std::vector<std::uint8_t> v;
    for (; h[0] != 0 && h[1] != 0; h += 2)
    {
        auto nib = [] (char c) { return (std::uint8_t) (c <= '9' ? c - '0' : c - 'a' + 10); };
        v.push_back ((std::uint8_t) (nib (h[0]) << 4 | nib (h[1])));
    }
    return v;
}

// FL's record of a new zone (the bytes of the zones in FL's shipped .dwp files, e.g. "Nylon Guitar.dwp"): tuning
// neutral, no stretch, sends ½ ½ ½ 1, both filters off (cutoff 0.472, res ½), the zone effects off, the modulation
// envelopes and LFOs at rest, the three default routes (velocity → gain 100 %, …) and 13 empty slots
const char* const kStretch = "00000000003f0000003f0000803f";
const char* const kSends = "0000003f0000003f0000003f0000803f0000000000000000000000000000000000000000000000000000000000000000";
const char* const kFilter = "00000000c8e3f13e0000003f0000000000000000";
const char* const kFx = "00000000000000803f";
const char* const kFxLast = "000000003f0000803f";
const char* const kModEnv = "000000000000003f0000003f0000803e";
const char* const kLfo1 = "00000000cdcccc3d0000803f0000000000000000";
const char* const kLfo2 = "00000000cdcc0c3f0000803f0000000000000000";
const char* const kRoutes[3] = { "020002000000803f", "030022000000403f", "0c0001000000003f" };
const char* const kRouteEmpty = "000000000000003f";
// the program's effects as FL's shipped programs have them (all off)
const char* const kDelay = "00000000403f0000403f0000003f0000403f";
const char* const kReverb = "000000803e0000003f0000403f0000803e";
const char* const kChorus = "000000803e0000003f0000803e00000000";
const char* const kProgLfo = "00000000cdcccc3d0000803f0000000000000000";

// seconds → DirectWave's time value (t = 10·v⁴ + 0.001 s)
float timeValue (double seconds)
{
    if (! (seconds > 0.001))
        return 0.0f;
    return (float) std::clamp (std::pow ((seconds - 0.001) / 10.0, 0.25), 0.0, 1.0);
}

void writeZone (Out& o, const Zone& z)
{
    const auto zone = o.open (3);
    {
        const auto at = o.open (0x1F4);
        o.u8 ((std::uint8_t) std::clamp (z.root, 0, 127));
        const auto lo = std::clamp (z.lowKey, 0, 127), lv = std::clamp (z.lowVel, 0, 127);
        o.u8 ((std::uint8_t) lo);
        o.u8 ((std::uint8_t) std::clamp (z.highKey, lo, 127));
        o.u8 ((std::uint8_t) lv);
        o.u8 ((std::uint8_t) std::clamp (z.highVel, lv, 127));
        o.u8 ((std::uint8_t) std::clamp (z.exclusiveGroup, 0, 255));
        o.u8 (0); o.u8 (0); o.u8 (0);                       // output, (unused), serial filters
        o.f32 ((float) std::clamp (std::pow (10.0, std::min (0.0, z.gainDb) / 20.0), 0.0, 1.0)); // linear (REA_rodada72 §3.5)
        o.f32 ((float) std::clamp (0.5 + 0.5 * z.pan, 0.0, 1.0));
        o.i32 (std::clamp (z.altGroup, 0, 99));
        o.u32 (512);
        o.close (at);
    }
    o.chunk (0x1F5, z.name);
    o.chunk (0x1F6, z.path);
    {
        const auto at = o.open (0x1F7);
        const auto frames = std::max<std::int64_t> (0, z.frames);
        o.i64 (frames);
        o.i32 (std::clamp (z.channels, 1, 2));
        o.i32 (16);                                         // beats (FL's default)
        o.f32 ((float) (z.rate > 0.0 ? z.rate : 44100.0));
        o.i32 (std::clamp (z.loopMode, 0, 4));
        auto clampPoint = [frames] (std::int64_t v) { return (std::int32_t) std::clamp<std::int64_t> (v, 0, frames > 0 ? frames : 0x7fffffff); };
        o.i32 (clampPoint (z.loopStart));
        o.i32 (clampPoint (z.loopEnd));
        o.i32 (clampPoint (z.offset));
        o.i32 (z.bits == 16 ? 16 : 32);
        o.close (at);
    }
    {
        // tuning: pitch ½ (none), coarse semitones, fine cents (both signed bytes), key tracking in cents per key
        auto coarse = z.coarse + z.fine / 100, fine = z.fine % 100;
        const auto at = o.open (0x1F8);
        o.f32 (0.5f);
        o.u8 ((std::uint8_t) (std::int8_t) std::clamp (coarse, -127, 127));
        o.u8 ((std::uint8_t) (std::int8_t) fine);
        o.u16 ((std::uint16_t) (std::int16_t) std::clamp (z.keyTrack, -1200, 1200));
        o.close (at);
    }
    o.chunk (0x1F9, hex (kStretch));
    o.chunk (0x1FA, hex (kSends));
    o.chunk (0x1FB, hex (kFilter));
    o.chunk (0x1FB, hex (kFilter));
    o.chunk (0x1FC, std::vector<std::uint8_t> { 0, 0 });
    {
        const auto at = o.open (0x1FD);
        o.f32 (z.attack >= 0.0 ? timeValue (z.attack) : 0.0f);
        o.f32 (z.decay >= 0.0 ? timeValue (z.decay) : 0.5f);
        // the envelope's level is heard squared (REA_rodada72 §3.9): the sustain value is the root of the amplitude
        o.f32 (z.sustain >= 0.0 ? (float) std::sqrt (std::clamp (z.sustain, 0.0, 1.0)) : 1.0f);
        o.f32 (z.release >= 0.0 ? timeValue (z.release) : 0.25f);
        o.close (at);
    }
    o.chunk (0x1FE, hex (kFx));
    o.chunk (0x1FF, hex (kFx));
    o.chunk (0x200, hex (kFx));
    o.chunk (0x201, hex (kFxLast));
    o.chunk (0x202, hex (kModEnv));
    o.chunk (0x202, hex (kModEnv));
    o.chunk (0x203, hex (kLfo1));
    o.chunk (0x203, hex (kLfo2));
    for (int i = 0; i < 16; ++i)
        o.chunk (0x204, hex (i < 3 ? kRoutes[i] : kRouteEmpty));
    o.chunk (4, std::vector<std::uint8_t> {});
    o.close (zone);
}

// ------------------------------------------------------------------------------------------------ reading chunks
std::uint32_t le32 (const std::uint8_t* p) { std::uint32_t v; std::memcpy (&v, p, 4); return v; }
std::uint64_t le64 (const std::uint8_t* p) { std::uint64_t v; std::memcpy (&v, p, 8); return v; }

// Copies a chunk list, checking that it parses (a program file's state, as DirectWave stores it). Short records stay
// as stored: dw::parseState completes them the way DirectWave reads them (DirectWave.cpp readZone, FL's
// read (min (size, record)) over a record of zeros, the sample format record's bit depth 32; round 190il — the
// shipped version-36 programs' 36-byte sample format records keep their loops, in .flp projects and the DirectWave
// editor's "Open program" too). Returns false when the list does not parse.
bool normalise (const std::uint8_t* p, std::size_t n, Out& o, int depth)
{
    std::size_t at = 0;
    while (at + 12 <= n)
    {
        const auto id = le32 (p + at);
        const auto size = le64 (p + at + 4);
        if (size > n - at - 12)
            return false;
        const auto* body = p + at + 12;
        if ((id == 1 && depth == 0) || (id == 3 && depth == 1))
        {
            const auto c = o.open (id);
            if (! normalise (body, (std::size_t) size, o, depth + 1))
                return false;
            o.close (c);
        }
        else
            o.chunk (id, std::vector<std::uint8_t> (body, body + size));
        at += 12 + (std::size_t) size;
    }
    return true;
}

std::string lower (std::string s)
{
    std::transform (s.begin(), s.end(), s.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
    return s;
}

std::string withoutExtension (const std::string& path)
{
    const auto dot = path.find_last_of ('.');
    const auto sep = path.find_last_of ("\\/");
    if (dot == std::string::npos || (sep != std::string::npos && dot < sep))
        return path;
    return path.substr (0, dot);
}

std::string folderOf (const std::string& path)
{
    const auto sep = path.find_last_of ("\\/");
    return sep == std::string::npos ? std::string() : path.substr (0, sep);
}

std::string join (const std::string& folder, const std::string& name, char separator)
{
    if (folder.empty())
        return name;
    const auto last = folder.back();
    return (last == '\\' || last == '/') ? folder + name : folder + separator + name;
}

bool fileExists (const Access& a, const std::string& path)
{
    return ! path.empty() && a.exists && a.exists (path);
}

std::optional<ChannelDef> fromState (const std::vector<std::uint8_t>& state, const std::string& programFile, const std::string& name,
                                     const Access& access, Report& report)
{
    const auto st = dw::parseState (state);
    if (! st || dw::playedProgram (*st) == nullptr)
    {
        report.error = "The file holds no program with zones.";
        return std::nullopt;
    }
    const auto* prog = dw::playedProgram (*st);
    report.name = prog->name;
    int missing = 0;
    const auto existing = [&] (const std::string& saved) {
        auto found = programFile.empty() ? (fileExists (access, saved) ? saved : std::string()) : findZoneSample (saved, programFile, access);
        if (found.empty())
            ++missing;
        return found;
    };
    const auto readFile = [&access] (const std::string& p) { return access.read ? access.read (p, (std::size_t) 1 << 16) : std::vector<std::uint8_t> {}; };
    ChannelDef def;
    def.name = prog->name.empty() ? name : prog->name;
    if (! dw::importChannel (def, state, existing, readFile, access.saveWav))
    {
        report.missingSamples = (int) prog->zones.size();
        report.error = "None of the program's samples was found.";
        return std::nullopt;
    }
    report.zones = (int) def.zones.size();
    report.missingSamples = missing;
    if (missing > 0)
        report.skipped.push_back (std::to_string (missing) + " sample file(s) not found (those zones stay silent)");
    return def;
}
} // namespace

// ================================================================================================= state
std::vector<std::uint8_t> buildState (const Program& program)
{
    Out o;
    o.u32 (38);
    o.chunk (6, std::vector<std::uint8_t> (16, 0));
    const auto prog = o.open (1);
    {
        const auto at = o.open (0x64);
        o.i32 (0);                                           // program index
        o.u8 (0); o.u8 (0);                                  // poly, glide always
        o.f32 (1.0f);                                        // program gain
        o.f32 (0.0f);                                        // glide
        o.u32 ((std::uint32_t) program.zones.size());
        for (int i = 0; i < 12; ++i)
            o.u8 (0);
        o.close (at);
    }
    o.chunk (0x66, program.name);
    o.chunk (0x68, std::vector<std::uint8_t> (10, 0));
    o.chunk (0x69, hex (kDelay));
    o.chunk (0x6A, hex (kReverb));
    o.chunk (0x6B, hex (kChorus));
    o.chunk (0x6C, hex (kProgLfo));
    o.chunk (0x6C, hex (kProgLfo));
    for (int i = 0; i < 4; ++i)
        o.chunk (0x6D, std::vector<std::uint8_t> (4, 0));
    for (int i = 0; i < 100; ++i)
    {
        int mode = 0;
        for (const auto& g : program.groups)
            if (g.index == i)
                mode = std::clamp (g.mode, 0, 3);
        const auto at = o.open (0x6E);
        o.i32 (i);
        o.u8 ((std::uint8_t) mode);
        o.f32 (1.0f);
        o.f32 (0.0f);
        o.close (at);
    }
    for (const auto& z : program.zones)
        writeZone (o, z);
    o.chunk (2, std::vector<std::uint8_t> {});
    o.close (prog);
    return std::move (o.b);
}

// ================================================================================================= files
std::optional<std::vector<std::uint8_t>> stateOfFile (const std::vector<std::uint8_t>& file)
{
    if (file.size() < 8 || (std::memcmp (file.data(), "DwPr", 4) != 0 && std::memcmp (file.data(), "DwBk", 4) != 0))
        return std::nullopt;
    const auto version = le32 (file.data() + 4);
    if (version < 36 || version > 38)
        return std::nullopt;
    Out o;
    o.u32 (version);
    if (! normalise (file.data() + 8, file.size() - 8, o, 0))
        return std::nullopt;
    return std::move (o.b);
}

std::string findZoneSample (const std::string& savedPath, const std::string& programFile, const Access& access)
{
    if (savedPath.empty())
        return {};
    // 1. as saved, with FL's path variables (%FLStudioData%, %ILSharedData%, …; %USERPROFILE% = this user's profile)
    auto saved = savedPath;
    if (lower (saved.substr (0, 13)) == "%userprofile%" && ! access.roots.home.empty())
        saved = access.roots.home + saved.substr (13);
    const auto resolved = resolveFlPath (saved, access.roots);
    if (fileExists (access, resolved))
        return resolved;
    if (fileExists (access, savedPath))
        return savedPath;
    const auto name = fileNameOf (savedPath);
    if (name.empty() || programFile.empty())
        return {};
    // (FL's names were written for a case-insensitive file system: on POSIX ones the spelling on disk is looked up)
    const auto found = [&access] (const std::string& p) -> std::string {
        if (fileExists (access, p))
            return p;
        if (access.separator == '/')
            if (auto q = existingPathIgnoringCase (p); q != p && fileExists (access, q))
                return q;
        return {};
    };
    // 2. DirectWave's own fallback (0x18001dc20): the folder named after the program file, next to it
    if (auto p = found (join (withoutExtension (programFile), name, access.separator)); ! p.empty())
        return p;
    // 3. (LYNX, for FL's host-side search) the program's folder, the saved path's own folder inside it (FL 2026 moved
    //    its packs: "...\Packs\Guitar\Bass\FLStudio Bass 02\x.wav" lives in "Packs\Legacy\Instruments\Guitar\Bass\
    //    FLStudio Bass 02" next to the program), then below it
    const auto folder = folderOf (programFile);
    if (auto p = found (join (folder, name, access.separator)); ! p.empty())
        return p;
    if (const auto savedFolder = fileNameOf (folderOf (savedPath)); ! savedFolder.empty())
        if (auto p = found (join (join (folder, savedFolder, access.separator), name, access.separator)); ! p.empty())
            return p;
    if (access.findBelow)
        if (auto p = access.findBelow (folder, name); fileExists (access, p))
            return p;
    // 4. DirectWave's own last step (0x180006590): its content library, searched for the name (FL's path manager hands
    //    the plugin the folder; FL's shipped programs point into %ILSharedData%\DirectWave)
    if (access.findBelow && ! access.roots.sharedData.empty())
        if (auto p = access.findBelow (join (access.roots.sharedData, "DirectWave", access.separator), name); fileExists (access, p))
            return p;
    return {};
}

bool handles (const std::string& extension)
{
    const auto e = lower (extension);
    return e == "dwp" || e == "dwb" || e == "gig" || e == "nki";
}

std::vector<std::string> instrumentNames (const std::string& path, const Access& access)
{
    if (extensionOf (path) == "gig")
        return gig::instrumentNames (path, access);
    return { fileNameOf (withoutExtension (path)) };
}

std::optional<ChannelDef> importFile (const std::string& path, const Access& access, Report& report, int instrument)
{
    const auto ext = extensionOf (path);
    const auto base = fileNameOf (withoutExtension (path));
    if (ext == "dwp" || ext == "dwb")
    {
        report.format = ext == "dwp" ? "DirectWave program" : "DirectWave bank";
        report.instruments = 1;
        // (a program holds audio only when saved with it; a bank holds 128 programs of ~3 KB)
        const auto bytes = access.read ? access.read (path, (std::size_t) 512 << 20) : std::vector<std::uint8_t> {};
        const auto state = stateOfFile (bytes);
        if (! state)
        {
            report.error = bytes.empty() ? "The file could not be read." : "This is not a DirectWave file LYNX SHARE can read.";
            return std::nullopt;
        }
        return fromState (*state, path, base, access, report);
    }
    if (ext == "gig")
    {
        report.format = "GigaStudio";
        auto program = gig::readInstrument (path, access, instrument, report);
        if (! program)
            return std::nullopt;
        return importProgram (std::move (*program), access, report);
    }
    if (ext == "nki")
    {
        report.format = "Kontakt";
        auto program = kontakt::readInstrument (path, access, report);
        if (! program)
            return std::nullopt;
        return importProgram (std::move (*program), access, report);
    }
    report.error = "This file type is not a sampler instrument LYNX SHARE imports.";
    return std::nullopt;
}

std::optional<ChannelDef> importProgram (Program program, const Access& access, Report& report)
{
    if (report.name.empty())
        report.name = program.name;
    int failed = 0;
    for (std::size_t i = 0; i < program.zones.size(); ++i)
    {
        auto& z = program.zones[i];
        if (z.path.empty() && ! z.wav.empty() && access.saveWav)
        {
            z.path = access.saveWav (z.wav, (program.name.empty() ? std::string ("sample") : program.name) + " "
                                                + (z.name.empty() ? std::to_string (i + 1) : z.name));
            if (z.path.empty())
                ++failed;
        }
        z.wav.clear();
        z.wav.shrink_to_fit();
    }
    if (failed > 0)
        report.skipped.push_back (std::to_string (failed) + " sample(s) could not be written to disk");
    program.zones.erase (std::remove_if (program.zones.begin(), program.zones.end(), [] (const Zone& z) { return z.path.empty(); }),
                         program.zones.end());
    if (program.zones.empty())
    {
        if (report.error.empty())
            report.error = "The instrument has no zone LYNX SHARE can play.";
        return std::nullopt;
    }
    const auto state = buildState (program);
    Access plain = access;
    return fromState (state, {}, program.name, plain, report);
}

// ================================================================================================= helpers
std::vector<std::uint8_t> wavFile (const std::uint8_t* data, std::size_t bytes, int channels, int bits, double rate,
                                   std::int64_t loopStart, std::int64_t loopEnd, int root)
{
    const bool isFloat = bits == -32;
    const int bytesPerSample = (isFloat ? 32 : bits) / 8;
    if (channels < 1 || channels > 8 || bytesPerSample < 1 || bytesPerSample > 4 || ! (rate >= 1000.0 && rate <= 768000.0))
        return {};
    const auto block = (std::size_t) (channels * bytesPerSample);
    bytes -= bytes % block;
    const bool smpl = loopEnd > loopStart && loopStart >= 0;
    if (bytes > 0xFFFFFF00u - 200)
        return {};
    Out o;
    o.str ("RIFF");
    const auto riff = o.b.size();
    o.u32 (0);
    o.str ("WAVEfmt ");
    o.u32 (16);
    o.u16 (isFloat ? 3 : 1);
    o.u16 ((std::uint16_t) channels);
    const auto r = (std::uint32_t) std::lround (rate);
    o.u32 (r);
    o.u32 (r * (std::uint32_t) block);
    o.u16 ((std::uint16_t) block);
    o.u16 ((std::uint16_t) (bytesPerSample * 8));
    if (smpl || root >= 0)
    {
        // 'smpl': the unity note and the first loop (end inclusive), as samplers read it
        o.str ("smpl");
        o.u32 (36 + (smpl ? 24 : 0));
        o.u32 (0); o.u32 (0);
        o.u32 ((std::uint32_t) std::llround (1.0e9 / rate));
        o.u32 ((std::uint32_t) std::clamp (root < 0 ? 60 : root, 0, 127));
        o.u32 (0); o.u32 (0); o.u32 (0);
        o.u32 (smpl ? 1 : 0);
        o.u32 (0);
        if (smpl)
        {
            o.u32 (0); o.u32 (0);
            o.u32 ((std::uint32_t) loopStart);
            o.u32 ((std::uint32_t) (loopEnd - 1));
            o.u32 (0); o.u32 (0);
        }
    }
    o.str ("data");
    o.u32 ((std::uint32_t) bytes);
    o.b.reserve (o.b.size() + bytes + 1);
    o.put (data, bytes);
    if (bytes & 1)
        o.u8 (0);
    const auto total = (std::uint32_t) (o.b.size() - 8);
    std::memcpy (o.b.data() + riff, &total, 4);
    return std::move (o.b);
}

std::string extensionOf (const std::string& path)
{
    const auto name = fileNameOf (path);
    const auto dot = name.find_last_of ('.');
    return dot == std::string::npos ? std::string() : lower (name.substr (dot + 1));
}

std::string fileNameOf (const std::string& path)
{
    const auto sep = path.find_last_of ("\\/");
    return sep == std::string::npos ? path : path.substr (sep + 1);
}
} // namespace lynx::msimport
