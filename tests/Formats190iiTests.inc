// Round 190ii (issue #149): DirectWave programs / banks (.dwp / .dwb), GigaStudio instruments (.gig), NCW samples and
// Kontakt detection. Every fixture is generated here (no third-party sample content). Spec:
// docs/analise/REA_rodada190ii.md.
#include "core/SamplerImport.h"
#include "core/GigFile.h"
#include "core/Ncw.h"
#include "core/KontaktFile.h"
#include "core/DirectWave.h"

namespace formats190ii
{
// ------------------------------------------------------------------------------------------------ an in-memory disk
struct Disk
{
    std::map<std::string, std::vector<std::uint8_t>> files;
    int saved = 0;

    lynx::msimport::Access access()
    {
        lynx::msimport::Access a;
        a.exists = [this] (const std::string& p) { return files.count (p) > 0; };
        a.read = [this] (const std::string& p, std::size_t limit) {
            const auto it = files.find (p);
            if (it == files.end())
                return std::vector<std::uint8_t> {};
            return std::vector<std::uint8_t> (it->second.begin(), it->second.begin() + (std::ptrdiff_t) std::min (limit, it->second.size()));
        };
        a.readAt = [this] (const std::string& p, std::uint64_t at, std::size_t n) {
            const auto it = files.find (p);
            if (it == files.end() || at > it->second.size() || n > it->second.size() - at)
                return std::vector<std::uint8_t> {};
            return std::vector<std::uint8_t> (it->second.begin() + (std::ptrdiff_t) at, it->second.begin() + (std::ptrdiff_t) (at + n));
        };
        a.size = [this] (const std::string& p) {
            const auto it = files.find (p);
            return it == files.end() ? (std::uint64_t) 0 : (std::uint64_t) it->second.size();
        };
        a.saveWav = [this] (const std::vector<std::uint8_t>& wav, const std::string& name) {
            const auto p = "/cache/" + name + "-" + std::to_string (++saved) + ".wav";
            files[p] = wav;
            return p;
        };
        a.separator = '/';
        a.roots.data = "/fl/Data";
        a.roots.sharedData = "/docs/Image-Line/";
        return a;
    }
};

// ------------------------------------------------------------------------------------------------ byte writers
struct Bytes
{
    std::vector<std::uint8_t> b;
    void u8 (unsigned v) { b.push_back ((std::uint8_t) v); }
    void u16 (unsigned v) { u8 (v & 0xFF); u8 ((v >> 8) & 0xFF); }
    void u32 (std::uint32_t v) { for (int k = 0; k < 4; ++k) u8 ((v >> (8 * k)) & 0xFF); }
    void u64 (std::uint64_t v) { for (int k = 0; k < 8; ++k) u8 ((unsigned) ((v >> (8 * k)) & 0xFF)); }
    void f32 (float f) { std::uint32_t v; std::memcpy (&v, &f, 4); u32 (v); }
    void str (const std::string& s) { b.insert (b.end(), s.begin(), s.end()); }
    void add (const std::vector<std::uint8_t>& v) { b.insert (b.end(), v.begin(), v.end()); }
};

// DirectWave chunk: u32 id, u64 size, body
std::vector<std::uint8_t> dwChunk (std::uint32_t id, const std::vector<std::uint8_t>& body)
{
    Bytes o;
    o.u32 (id);
    o.u64 (body.size());
    o.add (body);
    return o.b;
}

// A version-36 program file as FL's legacy packs have it: one zone, a 36-byte sample format record
std::vector<std::uint8_t> legacyDwp (const std::string& samplePath, int root, int lo, int hi, int loopMode, int loopA, int loopB)
{
    Bytes header;
    header.u8 ((unsigned) root); header.u8 ((unsigned) lo); header.u8 ((unsigned) hi); header.u8 (0); header.u8 (127);
    header.u8 (0); header.u8 (0); header.u8 (204); header.u8 (0);
    header.f32 (1.0f); header.f32 (0.5f); header.u32 (0); header.u32 (512);
    Bytes format;
    format.u64 (1000); format.u32 (1); format.u32 (16); format.f32 (44100.0f);
    format.u32 ((std::uint32_t) loopMode); format.u32 ((std::uint32_t) loopA); format.u32 ((std::uint32_t) loopB); format.u32 (0);
    Bytes zone;
    zone.add (dwChunk (0x1F4, header.b));
    zone.add (dwChunk (0x1F5, std::vector<std::uint8_t> { 'z' }));
    zone.add (dwChunk (0x1F6, std::vector<std::uint8_t> (samplePath.begin(), samplePath.end())));
    zone.add (dwChunk (0x1F7, format.b));
    zone.add (dwChunk (4, {}));
    Bytes prog;
    Bytes p64;
    p64.u32 (0); p64.u8 (0); p64.u8 (0); p64.f32 (1.0f); p64.f32 (0.0f);
    for (int i = 0; i < 20; ++i) p64.u8 (0);
    prog.add (dwChunk (0x64, p64.b));
    prog.add (dwChunk (0x66, std::vector<std::uint8_t> { 'P', 'i', 'a', 'n', 'o' }));
    prog.add (dwChunk (3, zone.b));
    prog.add (dwChunk (2, {}));
    Bytes f;
    f.str ("DwPr");
    f.u32 (36);
    f.add (dwChunk (6, std::vector<std::uint8_t> (16, 0)));
    f.add (dwChunk (1, prog.b));
    return f.b;
}

std::vector<std::uint8_t> tinyWav (int frames, int channels = 1)
{
    std::vector<std::int16_t> pcm ((std::size_t) (frames * channels));
    for (std::size_t i = 0; i < pcm.size(); ++i)
        pcm[i] = (std::int16_t) ((int) (i * 37) % 2000 - 1000);
    return lynx::msimport::wavFile ((const std::uint8_t*) pcm.data(), pcm.size() * 2, channels, 16, 44100.0);
}

// ------------------------------------------------------------------------------------------------ a synthetic .gig
std::vector<std::uint8_t> riffChunk (const char* id, const std::vector<std::uint8_t>& body)
{
    Bytes o;
    o.str (std::string (id, 4));
    o.u32 ((std::uint32_t) body.size());
    o.add (body);
    if (body.size() & 1)
        o.u8 (0);
    return o.b;
}

std::vector<std::uint8_t> riffList (const char* id, const char* type, const std::vector<std::vector<std::uint8_t>>& kids)
{
    Bytes body;
    body.str (std::string (type, 4));
    for (const auto& k : kids)
        body.add (k);
    return riffChunk (id, body.b);
}

std::vector<std::uint8_t> wsmp (int unity, int fine, std::int32_t gain, int loopStart = -1, int loopLength = 0)
{
    Bytes o;
    o.u32 (20); o.u16 ((unsigned) unity); o.u16 ((unsigned) (std::uint16_t) (std::int16_t) fine); o.u32 ((std::uint32_t) gain); o.u32 (0);
    o.u32 (loopStart >= 0 ? 1u : 0u);
    if (loopStart >= 0)
    {
        o.u32 (16); o.u32 (0); o.u32 ((std::uint32_t) loopStart); o.u32 ((std::uint32_t) loopLength);
    }
    return riffChunk ("wsmp", o.b);
}

// '3ewa' (148 bytes, gig 3): attack / decay 1 / sustain ‰ / release codes, pan, velocity upper limit (gig 2), the
// dimension upper limits (gig 3)
std::vector<std::uint8_t> ewa (std::int32_t attack, std::int32_t decay, int sustain, std::int32_t release, int pan, std::array<std::uint8_t, 8> upper,
                               int velUpper = 0, bool infinite = true)
{
    std::vector<std::uint8_t> b (148, 0);
    auto put32 = [&b] (std::size_t o, std::int32_t v) { std::memcpy (b.data() + o, &v, 4); };
    put32 (0, 148);
    put32 (28, attack);
    put32 (32, decay);
    b[38] = (std::uint8_t) (sustain & 0xFF); b[39] = (std::uint8_t) (sustain >> 8);
    put32 (40, release);
    put32 (80, infinite ? 0x7fffffff : 0);
    b[109] = (std::uint8_t) (pan >= 0 ? pan : 63 - pan);
    b[124] = (std::uint8_t) velUpper;
    std::memcpy (b.data() + 140, upper.data(), 8);
    return riffChunk ("3ewa", b);
}

std::vector<std::uint8_t> ewl (const std::vector<std::uint8_t>& sampler, const std::vector<std::uint8_t>& params)
{
    return riffList ("LIST", "3ewl", { sampler, params });
}

std::vector<std::uint8_t> info (const std::string& name)
{
    std::vector<std::uint8_t> s (name.begin(), name.end());
    s.push_back (0);
    return riffList ("LIST", "INFO", { riffChunk ("INAM", s) });
}

std::vector<std::uint8_t> rgnh (int lk, int hk, int lv, int hv, int keyGroup)
{
    Bytes o;
    o.u16 ((unsigned) lk); o.u16 ((unsigned) hk); o.u16 ((unsigned) lv); o.u16 ((unsigned) hv); o.u16 (0); o.u16 ((unsigned) keyGroup);
    return riffChunk ("rgnh", o.b);
}

// '3lnk' (gig 3): dimension definitions (type, bits, zones) and the wave pool index of every dimension region
std::vector<std::uint8_t> lnk (const std::vector<std::array<int, 3>>& dims, const std::vector<std::uint32_t>& waves)
{
    Bytes o;
    o.u32 ((std::uint32_t) waves.size());
    int pos = 0;
    for (int i = 0; i < 8; ++i)
    {
        if (i < (int) dims.size())
        {
            o.u8 ((unsigned) dims[(std::size_t) i][0]); o.u8 ((unsigned) dims[(std::size_t) i][1]); o.u8 ((unsigned) pos);
            o.u8 (0); o.u8 ((unsigned) dims[(std::size_t) i][2]); o.u8 (0); o.u8 (0); o.u8 (0);
            pos += dims[(std::size_t) i][1];
        }
        else
            for (int k = 0; k < 8; ++k) o.u8 (0);
    }
    for (const auto w : waves)
        o.u32 (w);
    return riffChunk ("3lnk", o.b);
}

std::vector<std::uint8_t> wave (int frames, int channels, bool compressed, const std::string& name)
{
    Bytes fmt;
    fmt.u16 (1); fmt.u16 ((unsigned) channels); fmt.u32 (44100); fmt.u32 (44100u * 2u * (unsigned) channels); fmt.u16 (2u * (unsigned) channels); fmt.u16 (16);
    std::vector<std::uint8_t> data ((std::size_t) (frames * channels * 2));
    for (std::size_t i = 0; i < data.size(); ++i)
        data[i] = (std::uint8_t) (i * 7);
    std::vector<std::vector<std::uint8_t>> kids { riffChunk ("fmt ", fmt.b), riffChunk ("data", data), info (name) };
    if (compressed)
        kids.push_back (riffChunk ("ewav", std::vector<std::uint8_t> (8, 0)));
    return riffList ("LIST", "wave", kids);
}

// Instrument "Test Keys": region A (keys 36…59) with a 2-zone velocity split (custom limit 63) × a 2-zone round robin
// × a release trigger; region B (60…72, key group 3) plain with a loop; region C (80…90) on the third wave, which is
// compressed; and a second instrument "Other"
std::vector<std::uint8_t> testGig()
{
    using namespace formats190ii;
    const std::array<std::uint8_t, 8> v63 { 63, 0, 0, 0, 0, 0, 0, 0 }, v127 { 127, 0, 0, 0, 0, 0, 0, 0 };
    // dimension region index = vel | rr << 1 | release << 2 (8 regions)
    std::vector<std::vector<std::uint8_t>> ewls;
    std::vector<std::uint32_t> waves;
    for (int i = 0; i < 8; ++i)
    {
        const int vel = i & 1;
        ewls.push_back (ewl (wsmp (48, -20, 655360 * 6), ewa (0, 0, 500, 0, 32, vel == 0 ? v63 : v127)));
        waves.push_back ((std::uint32_t) ((i >> 1) & 1));
    }
    const auto regionA = riffList ("LIST", "rgn2", { rgnh (36, 59, 0, 127, 0), lnk ({ { 0x82, 1, 2 }, { 0x86, 1, 2 }, { 0x84, 1, 2 } }, waves),
                                                     riffList ("LIST", "3prg", ewls) });
    const auto regionB = riffList ("LIST", "rgn2", { rgnh (60, 72, 0, 127, 3), lnk ({}, { 1 }),
                                                     riffList ("LIST", "3prg", { ewl (wsmp (66, 0, 0, 100, 400), ewa (0, 0, 1000, 0, 0, {})) }) });
    const auto regionC = riffList ("LIST", "rgn2", { rgnh (80, 90, 0, 127, 0), lnk ({}, { 2 }),
                                                     riffList ("LIST", "3prg", { ewl (wsmp (85, 0, 0), ewa (0, 0, 1000, 0, 0, {})) }) });
    const auto ins1 = riffList ("LIST", "ins ", { riffChunk ("insh", std::vector<std::uint8_t> (12, 0)), info ("Test Keys"),
                                                  riffList ("LIST", "lrgn", { regionA, regionB, regionC }) });
    const auto ins2 = riffList ("LIST", "ins ", { riffChunk ("insh", std::vector<std::uint8_t> (12, 0)), info ("Other"),
                                                  riffList ("LIST", "lrgn", {}) });
    // the wave pool: offsets of each wave LIST from the start of the pool's data
    const std::vector<std::vector<std::uint8_t>> pool { wave (1000, 1, false, "w0"), wave (2000, 2, false, "w1"), wave (500, 1, true, "w2") };
    Bytes ptbl;
    ptbl.u32 (8); ptbl.u32 ((std::uint32_t) pool.size());
    std::uint32_t at = 0;
    for (const auto& w : pool)
    {
        ptbl.u32 (0); ptbl.u32 (at); // (64-bit form: file number, offset)
        at += (std::uint32_t) w.size();
    }
    Bytes vers;
    vers.u16 (0); vers.u16 (3); vers.u16 (0); vers.u16 (0);
    return riffList ("RIFF", "DLS ", { riffChunk ("vers", vers.b), riffChunk ("colh", std::vector<std::uint8_t> { 2, 0, 0, 0 }),
                                       riffChunk ("ptbl", ptbl.b), riffList ("LIST", "lins", { ins1, ins2 }), riffList ("LIST", "wvpl", pool) });
}

// ------------------------------------------------------------------------------------------------ an NCW writer
// (test-only: delta blocks at the narrowest width ≥ 2, raw blocks when that reaches the depth, optional mid / side)
std::vector<std::uint8_t> ncwFile (const std::vector<std::vector<std::int32_t>>& ch, int bits, bool isFloat, bool midSide)
{
    const int channels = (int) ch.size();
    const auto frames = ch[0].size();
    const auto blocks = (frames + 511) / 512;
    auto pack = [] (Bytes& o, const std::int32_t* v, int w) {
        std::uint64_t acc = 0;
        int have = 0;
        for (int i = 0; i < 512; ++i)
        {
            acc |= ((std::uint64_t) (std::uint32_t) v[i] & (w == 32 ? 0xFFFFFFFFull : ((1ull << w) - 1))) << have;
            have += w;
            while (have >= 8)
            {
                o.u8 ((unsigned) (acc & 0xFF));
                acc >>= 8;
                have -= 8;
            }
        }
    };
    Bytes data;
    std::vector<std::uint32_t> table;
    for (std::size_t b = 0; b < blocks; ++b)
    {
        table.push_back ((std::uint32_t) data.b.size());
        std::vector<std::vector<std::int32_t>> s ((std::size_t) channels, std::vector<std::int32_t> (512));
        for (int c = 0; c < channels; ++c)
            for (std::size_t i = 0; i < 512; ++i)
            {
                const auto k = std::min (frames - 1, b * 512 + i);
                s[(std::size_t) c][i] = ch[(std::size_t) c][k];
            }
        if (midSide)
            for (std::size_t i = 0; i < 512; ++i)
            {
                const auto l = s[0][i], r = s[1][i];
                s[0][i] = (l + r) / 2, s[1][i] = (l - r) / 2;
            }
        for (int c = 0; c < channels; ++c)
        {
            const auto& v = s[(std::size_t) c];
            std::int32_t d[512];
            int w = 2;
            for (int i = 0; i < 511; ++i)
            {
                d[i] = (std::int32_t) ((std::uint32_t) v[(std::size_t) i + 1] - (std::uint32_t) v[(std::size_t) i]);
                while (w < 32 && (d[i] < -(1ll << (w - 1)) || d[i] >= (1ll << (w - 1))))
                    ++w;
            }
            d[511] = 0;
            const bool raw = w >= bits;
            data.u32 (0x3E9A0C16u);
            data.u32 ((std::uint32_t) v[0]);
            data.u16 ((unsigned) (std::uint16_t) (std::int16_t) (raw ? -bits : w));
            data.u16 ((unsigned) ((isFloat ? 2 : 0) | (midSide ? 1 : 0)));
            data.u32 (0);
            pack (data, raw ? v.data() : d, raw ? bits : w);
        }
    }
    table.push_back ((std::uint32_t) data.b.size());
    Bytes f;
    for (const auto x : { 0x01, 0xA8, 0x9E, 0xD6, 0x31, 0x01, 0x00, 0x00 })
        f.u8 ((unsigned) x);
    f.u16 ((unsigned) channels); f.u16 ((unsigned) bits); f.u32 (48000); f.u32 ((std::uint32_t) frames); f.u32 (120);
    f.u32 ((std::uint32_t) (120 + 4 * table.size())); f.u32 ((std::uint32_t) data.b.size()); f.u32 (isFloat ? 1 : 0);
    while (f.b.size() < 120) f.u8 (0);
    for (const auto t : table) f.u32 (t);
    f.add (data.b);
    return f.b;
}

std::uint32_t lcg (std::uint32_t& s) { s = s * 1664525u + 1013904223u; return s >> 8; }
} // namespace formats190ii

// =================================================================================================== state builder
TEST (formats190ii_state_round_trips)
{
    using namespace lynx;
    msimport::Program p;
    p.name = "Round trip";
    msimport::Zone a;
    a.path = "/s/a.wav"; a.root = 48; a.lowKey = 36; a.highKey = 59; a.lowVel = 0; a.highVel = 63; a.fine = 130; a.gainDb = -6.0;
    a.pan = -0.5; a.loopMode = 2; a.loopStart = 100; a.loopEnd = 900; a.frames = 1000; a.exclusiveGroup = 3; a.altGroup = 7;
    a.attack = 0.5; a.decay = 1.0; a.sustain = 0.25; a.release = 2.0; a.keyTrack = 0;
    msimport::Zone b;
    b.path = "/s/b.wav"; b.root = 60; b.lowKey = 60; b.highKey = 200; b.lowVel = 90; b.highVel = 10; b.gainDb = 6.0;
    p.zones = { a, b };
    p.groups = { { 7, 1 } };
    const auto st = dw::parseState (msimport::buildState (p));
    CHECK (st.has_value());
    if (! st)
        return;
    const auto* prog = dw::playedProgram (*st);
    CHECK (prog != nullptr && prog->zones.size() == 2 && prog->name == "Round trip");
    if (prog == nullptr || prog->zones.size() != 2)
        return;
    const auto& z = prog->zones[0];
    CHECK (z.root == 48 && z.lowKey == 36 && z.highKey == 59 && z.lowVel == 0 && z.highVel == 63);
    CHECK (z.coarse == 1 && z.fine == 30 && z.keyTrack == 0);                    // 130 cents = 1 semitone + 30
    CHECK (std::fabs (z.gain - 0.501187f) < 1e-4f && std::fabs (z.pan - 0.25f) < 1e-6f);
    CHECK (z.loopMode == 2 && z.loopA == 100 && z.loopB == 900 && z.frames == 1000 && z.bits == 16);
    CHECK (z.exclusiveGroup == 3 && z.altGroup == 7 && prog->groups[7].mode == 1 && prog->groups[1].mode == 0);
    CHECK (z.path == "/s/a.wav");
    // the envelope in DirectWave's laws: t = 10·v⁴ + 0.001 s, the sustain level heard squared
    auto seconds = [] (float v) { return 10.0 * std::pow ((double) v, 4.0) + 0.001; };
    CHECK (std::fabs (seconds (z.amp[0]) - 0.5) < 1e-4 && std::fabs (seconds (z.amp[1]) - 1.0) < 1e-4 && std::fabs (seconds (z.amp[3]) - 2.0) < 1e-4);
    CHECK (std::fabs (z.amp[2] * z.amp[2] - 0.25f) < 1e-5f);
    // FL's new-zone defaults where nothing was said (the three default routes, the filters off)
    CHECK (z.mods[0].src == 2 && z.mods[0].dst == 2 && z.mods[0].amount == 1.0f && z.filter[0].type == 0);
    const auto& y = prog->zones[1];
    CHECK (y.highKey == 127 && y.lowVel == 90 && y.highVel == 90 && y.gain == 1.0f); // clamped: no inverted ranges, ≤ 0 dB
    CHECK (std::fabs (y.amp[1] - 0.5f) < 1e-6f && y.amp[2] == 1.0f && std::fabs (y.amp[3] - 0.25f) < 1e-6f);
}

// =================================================================================================== .dwp / .dwb
TEST (formats190ii_dwp_v36_format_record_completed)
{
    using namespace lynx;
    const auto file = formats190ii::legacyDwp ("%FLStudioData%\\Patches\\Packs\\Piano\\Piano 2\\a.wav", 45, 44, 46, 2, 300, 800);
    // LYNX's reader used to skip the 36-byte record (and with it the loop); DirectWave reads it partly, bits = 32 (readZone
    // does so since the 0.1.30 rollup; tests/Rollup030Tests.inc)
    const auto state = msimport::stateOfFile (file);
    CHECK (state.has_value());
    if (! state)
        return;
    const auto st = dw::parseState (*state);
    CHECK (st && dw::playedProgram (*st) != nullptr);
    if (! st || dw::playedProgram (*st) == nullptr)
        return;
    const auto& z = dw::playedProgram (*st)->zones.at (0);
    CHECK (z.loopMode == 2 && z.loopA == 300 && z.loopB == 800 && z.frames == 1000 && z.bits == 32 && z.root == 45);
    // a bank file is the state after "DwBk"
    auto bank = file;
    std::memcpy (bank.data(), "DwBk", 4);
    CHECK (msimport::stateOfFile (bank).has_value());
    auto bad = file;
    bad[4] = 39; // (unknown version)
    CHECK (! msimport::stateOfFile (bad).has_value());
}

TEST (formats190ii_dwp_samples_found_like_directwave)
{
    using namespace lynx;
    formats190ii::Disk disk;
    auto a = disk.access();
    const std::string dwp = "/fl/Data/Patches/Packs/Legacy/Instruments/Piano/Piano 2.dwp";
    // 1. the saved path with FL's variables
    disk.files["/fl/Data/Patches/x.wav"] = {};
    CHECK (msimport::findZoneSample ("%FLStudioData%\\Patches\\x.wav", dwp, a) == "/fl/Data/Patches/x.wav");
    disk.files["/docs/Image-Line/DirectWave/n.wav"] = {};
    CHECK (msimport::findZoneSample ("%ILSharedData%\\DirectWave\\n.wav", dwp, a) == "/docs/Image-Line/DirectWave/n.wav");
    // 2. moved pack: the folder named after the program file (DirectWave 0x18001dc20)
    disk.files["/fl/Data/Patches/Packs/Legacy/Instruments/Piano/Piano 2/Gz_A0ogg.wav"] = {};
    CHECK (msimport::findZoneSample ("%FLStudioData%\\Patches\\Packs\\Piano\\Piano 2\\Gz_A0ogg.wav", dwp, a)
           == "/fl/Data/Patches/Packs/Legacy/Instruments/Piano/Piano 2/Gz_A0ogg.wav");
    // 3. (LYNX) next to the program file, then below it
    disk.files["/fl/Data/Patches/Packs/Legacy/Instruments/Piano/kick.wav"] = {};
    CHECK (msimport::findZoneSample ("D:\\old\\kick.wav", dwp, a) == "/fl/Data/Patches/Packs/Legacy/Instruments/Piano/kick.wav");
    a.findBelow = [] (const std::string& folder, const std::string& name) { return folder + "/deep/" + name; };
    disk.files["/fl/Data/Patches/Packs/Legacy/Instruments/Piano/deep/snare.wav"] = {};
    CHECK (msimport::findZoneSample ("D:\\old\\snare.wav", dwp, a) == "/fl/Data/Patches/Packs/Legacy/Instruments/Piano/deep/snare.wav");
    CHECK (msimport::findZoneSample ("D:\\old\\none.wav", dwp, a).empty());
}

TEST (formats190ii_dwp_imports_as_directwave_channel)
{
    using namespace lynx;
    formats190ii::Disk disk;
    const std::string dwp = "/packs/Piano 2.dwp";
    disk.files[dwp] = formats190ii::legacyDwp ("%FLStudioData%\\Patches\\Packs\\Piano\\Piano 2\\a.wav", 45, 44, 46, 2, 300, 800);
    disk.files["/packs/Piano 2/a.wav"] = formats190ii::tinyWav (1000);
    msimport::Report r;
    const auto c = msimport::importFile (dwp, disk.access(), r);
    CHECK (c.has_value() && r.error.empty());
    if (! c)
        return;
    CHECK (c->instrument == InstrumentKind::directwave && c->zones.size() == 1 && c->zones[0].path == "/packs/Piano 2/a.wav");
    CHECK (c->name == "Piano" && r.zones == 1 && r.missingSamples == 0 && r.skipped.empty());
    CHECK (dw::decodeFormats (c->modelParams) == std::vector<bool> { true }); // a 16-bit PCM file: held as integers
    // the channel keeps the program's state as stored; readZone completes its 36-byte record, so the loop survives
    const auto st = dw::parseState (c->pluginState);
    CHECK (st && dw::playedProgram (*st)->zones.at (0).loopB == 800);
    // a missing sample: reported; all missing: no channel
    disk.files.erase ("/packs/Piano 2/a.wav");
    msimport::Report r2;
    CHECK (! msimport::importFile (dwp, disk.access(), r2).has_value() && ! r2.error.empty());
}

// =================================================================================================== .gig
TEST (formats190ii_gig_regions_and_dimensions)
{
    using namespace lynx;
    formats190ii::Disk disk;
    disk.files["/lib/keys.gig"] = formats190ii::testGig();
    const auto a = disk.access();
    CHECK (msimport::instrumentNames ("/lib/keys.gig", a) == std::vector<std::string> ({ "Test Keys", "Other" }));
    msimport::Report r;
    const auto c = msimport::importFile ("/lib/keys.gig", a, r);
    CHECK (c.has_value() && r.instruments == 2 && r.name == "Test Keys");
    if (! c)
        return;
    // region A: 2 velocity zones × 2 round-robin zones (release-trigger half skipped); region B: 1 zone
    CHECK (c->instrument == InstrumentKind::directwave && c->zones.size() == 5 && r.zones == 5);
    const auto st = dw::parseState (c->pluginState);
    CHECK (st.has_value());
    if (! st)
        return;
    const auto& zs = dw::playedProgram (*st)->zones;
    CHECK (zs.size() == 5);
    if (zs.size() != 5)
        return;
    // dimension region order: vel 0 rr 0, vel 1 rr 0, vel 0 rr 1, vel 1 rr 1
    CHECK (zs[0].lowVel == 0 && zs[0].highVel == 63 && zs[1].lowVel == 64 && zs[1].highVel == 127);
    CHECK (zs[2].lowVel == 0 && zs[2].highVel == 63 && zs[3].lowVel == 64 && zs[3].highVel == 127);
    CHECK (zs[0].lowKey == 36 && zs[0].highKey == 59 && zs[0].root == 48 && zs[0].coarse == 0 && zs[0].fine == -20);
    CHECK (zs[0].altGroup != 0 && zs[0].altGroup == zs[3].altGroup && dw::playedProgram (*st)->groups[(std::size_t) zs[0].altGroup].mode == 1);
    CHECK (std::fabs (zs[0].gain - 0.501187f) < 1e-4f && zs[0].pan > 0.5f);          // −6 dB, panned right
    CHECK (std::fabs (zs[0].amp[2] * zs[0].amp[2] - 0.5f) < 1e-5f);                  // sustain 500 ‰
    // round robin: the zones use the two waves (mono 1000 frames, stereo 2000)
    CHECK (zs[0].frames == 1000 && zs[0].channels == 1 && zs[2].frames == 2000 && zs[2].channels == 2);
    // region B: key group → exclusive group, the loop (start 100, length 400)
    CHECK (zs[4].lowKey == 60 && zs[4].highKey == 72 && zs[4].exclusiveGroup == 3 && zs[4].altGroup == 0);
    CHECK (zs[4].loopMode == 2 && zs[4].loopA == 100 && zs[4].loopB == 500 && zs[4].root == 66);
    // two WAV files written (the shared waves once), and what was left out is reported
    CHECK (disk.saved == 2);
    bool release = false, compressed = false;
    for (const auto& s : r.skipped)
    {
        release = release || s.find ("release trigger") != std::string::npos;
        compressed = compressed || s.find ("compressed") != std::string::npos;
    }
    CHECK (release && compressed);
    // the written WAVs hold the pool's audio as is
    const auto& wav = disk.files[c->zones[0].path];
    CHECK (wav.size() > 44 && std::memcmp (wav.data(), "RIFF", 4) == 0);
    // the second instrument has no region
    msimport::Report r2;
    CHECK (! msimport::importFile ("/lib/keys.gig", a, r2, 1).has_value() && ! r2.error.empty());
    msimport::Report r3;
    CHECK (! msimport::importFile ("/lib/keys.gig", a, r3, 5).has_value());
}

TEST (formats190ii_gig_time_codes)
{
    // GigaStudio stores times as 1.000000008813822^code seconds
    CHECK (std::fabs (lynx::msimport::gig::timeOf (0) - 1.0) < 1e-12);
    CHECK (std::fabs (lynx::msimport::gig::timeOf (-262471091) - 0.0989) < 1e-3);
}

TEST (formats190ii_malformed_files_do_not_crash)
{
    using namespace lynx;
    std::uint32_t seed = 190;
    const auto gigBytes = formats190ii::testGig();
    const auto dwpBytes = formats190ii::legacyDwp ("x.wav", 60, 0, 127, 2, 1, 2);
    std::vector<std::int32_t> tone (3000);
    for (std::size_t i = 0; i < tone.size(); ++i)
        tone[i] = (std::int32_t) (std::sin ((double) i * 0.05) * 20000.0);
    const auto ncwBytes = formats190ii::ncwFile ({ tone, tone }, 16, false, false);
    int survived = 0;
    for (int round = 0; round < 1500; ++round)
    {
        auto mutate = [&seed] (std::vector<std::uint8_t> v) {
            const auto kind = formats190ii::lcg (seed) % 3;
            if (kind == 0 && ! v.empty())
                v.resize (formats190ii::lcg (seed) % v.size());
            else
                for (int k = 0, n = 1 + (int) (formats190ii::lcg (seed) % 8); k < n && ! v.empty(); ++k)
                    v[formats190ii::lcg (seed) % v.size()] = (std::uint8_t) formats190ii::lcg (seed);
            return v;
        };
        formats190ii::Disk disk;
        disk.files["/f.gig"] = mutate (gigBytes);
        disk.files["/f.dwp"] = mutate (dwpBytes);
        disk.files["/x.wav"] = formats190ii::tinyWav (10);
        msimport::Report r1, r2;
        (void) msimport::importFile ("/f.gig", disk.access(), r1);
        (void) msimport::instrumentNames ("/f.gig", disk.access());
        (void) msimport::importFile ("/f.dwp", disk.access(), r2);
        const auto n = mutate (ncwBytes);
        ncw::Decoded d;
        if (ncw::decode (n.data(), n.size(), d))
            CHECK (d.channels.size() == 2 && d.channels[0].size() <= 3000);
        (void) ncw::toWav (n.data(), n.size());
        // every WAV the importer wrote stays far below the input's size bound
        for (const auto& [path, bytes] : disk.files)
            if (path.rfind ("/cache/", 0) == 0)
                CHECK (bytes.size() < 64 * 1024);
        ++survived;
    }
    CHECK (survived == 1500);
    // a header asking for a huge sample count is refused before any allocation
    auto huge = ncwBytes;
    huge[16] = 0xFF; huge[17] = 0xFF; huge[18] = 0xFF; huge[19] = 0x7F;
    ncw::Decoded d;
    CHECK (! ncw::decode (huge.data(), huge.size(), d));
}

// =================================================================================================== NCW
TEST (formats190ii_ncw_decodes_delta_raw_midside_float)
{
    using namespace lynx;
    std::vector<std::int32_t> l (1300), r (1300), loud (700), noise (600);
    std::uint32_t s = 7;
    for (std::size_t i = 0; i < l.size(); ++i)
    {
        l[i] = (std::int32_t) (std::sin ((double) i * 0.01) * 30000.0) & ~1;  // (even sums: lossless mid / side)
        r[i] = (std::int32_t) (std::cos ((double) i * 0.013) * 20000.0) & ~1;
    }
    for (auto& v : noise) // full-scale noise: raw blocks
        v = (std::int32_t) (formats190ii::lcg (s) & 0xFFFF) - 32768;
    for (std::size_t i = 0; i < loud.size(); ++i)
        loud[i] = (std::int32_t) (std::sin ((double) i * 0.02) * 8000000.0);
    // 16-bit stereo, direct and mid / side
    for (const bool ms : { false, true })
    {
        const auto f = formats190ii::ncwFile ({ l, r }, 16, false, ms);
        ncw::Decoded d;
        CHECK (ncw::isNcw (f.data(), f.size()) && ncw::decode (f.data(), f.size(), d));
        CHECK (d.info.channels == 2 && d.info.bits == 16 && d.info.frames == 1300 && d.info.sampleRate == 48000.0);
        bool same = d.channels.size() == 2;
        for (std::size_t i = 0; same && i < l.size(); ++i)
            same = d.channels[0][i] == (float) l[i] / 32768.0f && d.channels[1][i] == (float) r[i] / 32768.0f;
        CHECK (same);
    }
    // raw 16-bit blocks
    {
        const auto f = formats190ii::ncwFile ({ noise }, 16, false, false);
        ncw::Decoded d;
        CHECK (ncw::decode (f.data(), f.size(), d) && d.channels.size() == 1);
        bool same = true;
        for (std::size_t i = 0; same && i < noise.size(); ++i)
            same = d.channels[0][i] == (float) noise[i] / 32768.0f;
        CHECK (same);
        // the WAV the importer writes: 16-bit PCM, the same integers
        const auto w = ncw::toWav (f.data(), f.size());
        CHECK (w.size() == 44 + 2 * noise.size() && std::memcmp (w.data() + 8, "WAVE", 4) == 0);
        std::int16_t first = 0;
        std::memcpy (&first, w.data() + 44, 2);
        CHECK (first == (std::int16_t) noise[0]);
    }
    // 24-bit
    {
        const auto f = formats190ii::ncwFile ({ loud }, 24, false, false);
        ncw::Decoded d;
        CHECK (ncw::decode (f.data(), f.size(), d) && d.info.bits == 24);
        CHECK (d.channels.size() == 1 && std::fabs (d.channels[0][100] - (float) ((double) loud[100] / 8388608.0)) < 1e-7f);
    }
    // 32-bit floats: deltas on the bit patterns
    {
        std::vector<std::int32_t> bitsOf (600);
        for (std::size_t i = 0; i < bitsOf.size(); ++i)
        {
            const float v = (float) std::sin ((double) i * 0.03) * 0.5f;
            std::memcpy (&bitsOf[i], &v, 4);
        }
        const auto f = formats190ii::ncwFile ({ bitsOf }, 32, true, false);
        ncw::Decoded d;
        CHECK (ncw::decode (f.data(), f.size(), d) && d.info.isFloat);
        CHECK (d.channels.size() == 1 && d.channels[0][123] == (float) std::sin (123 * 0.03) * 0.5f);
    }
    // widths 0 / 1 are refused (no decoder exists for them)
    {
        auto f = formats190ii::ncwFile ({ l }, 16, false, false);
        const std::size_t sub = 120 + 4 * 4; // first sub-block (3 blocks + sentinel)
        f[sub + 8] = 1; f[sub + 9] = 0;
        ncw::Decoded d;
        CHECK (! ncw::decode (f.data(), f.size(), d));
    }
}

// =================================================================================================== Kontakt
TEST (formats190ii_kontakt_detected_never_decrypted)
{
    using namespace lynx;
    using msimport::kontakt::Kind;
    formats190ii::Bytes k1;
    k1.u32 (0x5EE56EB3u);
    for (int i = 0; i < 40; ++i) k1.u8 (0);
    CHECK (msimport::kontakt::detect (k1.b.data(), k1.b.size()) == Kind::kontakt1);
    formats190ii::Bytes k2;
    k2.u32 (0x1290A87Fu); k2.u32 (0x1000); k2.u16 (0x100);
    for (int i = 0; i < 40; ++i) k2.u8 (0);
    CHECK (msimport::kontakt::detect (k2.b.data(), k2.b.size()) == Kind::kontakt2);
    // an NI container item whose data chunks hold an Encryption Item (type 116)
    auto container = [] (std::uint32_t type) {
        formats190ii::Bytes chunk; // the terminator after the item
        formats190ii::Bytes term;
        term.u64 (20); term.str ("hsin"); term.u32 (1); term.u32 (1);
        chunk.u64 (20 + term.b.size()); chunk.str ("hsin"); chunk.u32 (type); chunk.u32 (1); chunk.add (term.b);
        formats190ii::Bytes item;
        formats190ii::Bytes body;
        body.u32 (1); body.str ("hsin"); body.u32 (1); body.u32 (0);
        for (int i = 0; i < 16; ++i) body.u8 (0);
        body.add (chunk.b);
        body.u32 (1); body.u32 (0); // version, no children
        item.u64 (8 + body.b.size());
        item.add (body.b);
        return item.b;
    };
    const auto locked = container (116), open = container (109);
    CHECK (msimport::kontakt::detect (locked.data(), locked.size()) == Kind::container);
    CHECK (msimport::kontakt::protectedContainer (locked.data(), locked.size()));
    CHECK (! msimport::kontakt::protectedContainer (open.data(), open.size()));
    // the import says what the file is and imports nothing
    formats190ii::Disk disk;
    disk.files["/k/lib.nki"] = locked;
    msimport::Report r;
    CHECK (! msimport::importFile ("/k/lib.nki", disk.access(), r).has_value() && r.error.find ("protected") != std::string::npos);
    CHECK (msimport::handles ("NKI") && msimport::handles ("gig") && msimport::handles ("dwp") && ! msimport::handles ("wav"));
}

// FL's own shipped programs (run with LYNX_FL_DIR=<FL Studio folder>; skipped otherwise): every one imports with
// every sample found, and the loops of the version-36 programs survive
TEST (formats190ii_fl_shipped_programs)
{
    const char* dir = std::getenv ("LYNX_FL_DIR");
    if (dir == nullptr || ! std::filesystem::is_directory (dir))
        return;
    namespace fs = std::filesystem;
    lynx::msimport::Access a;
    a.exists = [] (const std::string& p) { std::error_code e; return fs::is_regular_file (fs::path (p), e); };
    a.read = [] (const std::string& p, std::size_t limit) {
        std::ifstream f (p, std::ios::binary);
        std::vector<std::uint8_t> v ((std::istreambuf_iterator<char> (f)), {});
        if (v.size() > limit)
            v.resize (limit);
        return v;
    };
    a.separator = '/';
    a.roots.factoryData = dir;
    a.roots.data = std::string (dir) + "/Data";
    a.findBelow = [] (const std::string& folder, const std::string& name) -> std::string {
        std::error_code e;
        int seen = 0;
        for (auto it = fs::recursive_directory_iterator (folder, e); it != fs::recursive_directory_iterator() && ++seen < 20000; it.increment (e))
        {
            auto a = it->path().filename().string(), b = name;
            for (auto* s : { &a, &b })
                for (auto& ch : *s) ch = (char) std::tolower ((unsigned char) ch);
            if (a == b)
                return it->path().string();
        }
        return {};
    };
    int files = 0, ok = 0, zones = 0, missing = 0, looped = 0;
    std::error_code e;
    for (auto it = fs::recursive_directory_iterator (dir, e); it != fs::recursive_directory_iterator(); it.increment (e))
    {
        if (it->path().extension() != ".dwp")
            continue;
        ++files;
        lynx::msimport::Report r;
        const auto c = lynx::msimport::importFile (it->path().string(), a, r);
        if (! c)
        {
            std::printf ("  dwp not imported: %s (%s)\n", it->path().string().c_str(), r.error.c_str());
            continue;
        }
        ++ok;
        zones += r.zones;
        missing += r.missingSamples;
        if (const auto st = lynx::dw::parseState (c->pluginState))
            for (const auto& z : lynx::dw::playedProgram (*st)->zones)
                looped += z.loopMode == 2 && z.loopB > z.loopA ? 1 : 0;
    }
    std::printf ("  FL .dwp: %d files, %d imported, %d zones, %d samples missing, %d forward loops\n", files, ok, zones, missing, looped);
    CHECK (files > 0 && ok == files && missing == 0 && looped > 0);
}
