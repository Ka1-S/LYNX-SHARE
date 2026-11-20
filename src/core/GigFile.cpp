#include "core/GigFile.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <set>

namespace lynx::msimport::gig
{
namespace
{
constexpr std::uint32_t fourcc (const char* s)
{
    return (std::uint32_t) (std::uint8_t) s[0] | (std::uint32_t) (std::uint8_t) s[1] << 8 | (std::uint32_t) (std::uint8_t) s[2] << 16
           | (std::uint32_t) (std::uint8_t) s[3] << 24;
}

constexpr auto kRiff = fourcc ("RIFF"), kList = fourcc ("LIST"), kDls = fourcc ("DLS "), kVers = fourcc ("vers"),
               kPtbl = fourcc ("ptbl"), kLins = fourcc ("lins"), kIns = fourcc ("ins "), kInsh = fourcc ("insh"),
               kLrgn = fourcc ("lrgn"), kRgn = fourcc ("rgn "), kRgn2 = fourcc ("rgn2"), kRgnh = fourcc ("rgnh"),
               kWsmp = fourcc ("wsmp"), kWlnk = fourcc ("wlnk"), k3lnk = fourcc ("3lnk"), k3prg = fourcc ("3prg"),
               k3ewl = fourcc ("3ewl"), k3ewa = fourcc ("3ewa"), kWvpl = fourcc ("wvpl"), kDwpl = fourcc ("dwpl"),
               kWave = fourcc ("wave"), kFmt = fourcc ("fmt "), kData = fourcc ("data"), kEwav = fourcc ("ewav"),
               kInfo = fourcc ("INFO"), kInam = fourcc ("INAM"), kXfil = fourcc ("xfil"), kSmpl = fourcc ("smpl");

constexpr int kMaxChunks = 400000;            // per file walk (a large library has a few tens of thousands)
constexpr std::size_t kMaxSampleBytes = (std::size_t) 512 << 20;
constexpr std::size_t kMaxSmallChunk = 1 << 20; // headers, parameter chunks, pool tables

std::uint16_t u16 (const std::vector<std::uint8_t>& b, std::size_t o) { return o + 2 <= b.size() ? (std::uint16_t) (b[o] | b[o + 1] << 8) : 0; }
std::uint32_t u32 (const std::vector<std::uint8_t>& b, std::size_t o)
{
    if (o + 4 > b.size())
        return 0;
    std::uint32_t v; std::memcpy (&v, b.data() + o, 4); return v;
}
std::int32_t i32 (const std::vector<std::uint8_t>& b, std::size_t o) { return (std::int32_t) u32 (b, o); }
std::int16_t i16 (const std::vector<std::uint8_t>& b, std::size_t o) { return (std::int16_t) u16 (b, o); }
std::uint8_t u8 (const std::vector<std::uint8_t>& b, std::size_t o) { return o < b.size() ? b[o] : 0; }

// ------------------------------------------------------------------------------------------------ RIFF walking
struct Ck
{
    std::uint32_t id = 0, type = 0;  // type: the list type of RIFF / LIST chunks
    std::uint64_t at = 0;            // the chunk's header
    std::uint64_t data = 0, size = 0; // its body (lists: after the type)
    bool list() const { return id == kRiff || id == kList; }
};

struct File
{
    const Access& a;
    std::string path;
    std::uint64_t length = 0;
    int budget = kMaxChunks;

    File (const Access& access, std::string p) : a (access), path (std::move (p)) { length = a.size ? a.size (path) : 0; }

    std::vector<std::uint8_t> read (std::uint64_t at, std::size_t n) const
    {
        if (! a.readAt || at >= length || n == 0)
            return {};
        n = (std::size_t) std::min<std::uint64_t> (n, length - at);
        auto v = a.readAt (path, at, n);
        return v.size() == n ? v : std::vector<std::uint8_t> {};
    }

    // the chunk whose header is at `at` and that ends by `end`
    std::optional<Ck> chunkAt (std::uint64_t at, std::uint64_t end)
    {
        if (at + 8 > end || --budget < 0)
            return std::nullopt;
        const auto h = read (at, 12);
        if (h.size() < 8)
            return std::nullopt;
        Ck c;
        c.id = u32 (h, 0);
        c.at = at;
        const std::uint64_t size = u32 (h, 4);
        if (at + 8 + size > end)
            return std::nullopt;
        c.data = at + 8;
        c.size = size;
        if (c.list())
        {
            if (size < 4 || h.size() < 12)
                return std::nullopt;
            c.type = u32 (h, 8);
            c.data += 4;
            c.size -= 4;
        }
        return c;
    }

    std::vector<Ck> children (const Ck& parent)
    {
        std::vector<Ck> out;
        const auto end = parent.data + parent.size;
        for (auto at = parent.data; at + 8 <= end;)
        {
            const auto c = chunkAt (at, end);
            if (! c)
                break;
            out.push_back (*c);
            const auto next = c->data + c->size;
            at = next + (next & 1); // (RIFF pads chunks to even sizes)
        }
        return out;
    }

    std::vector<std::uint8_t> body (const Ck& c, std::size_t limit = kMaxSmallChunk) { return read (c.data, (std::size_t) std::min<std::uint64_t> (c.size, limit)); }
};

const Ck* find (const std::vector<Ck>& list, std::uint32_t id, std::uint32_t type = 0)
{
    for (const auto& c : list)
        if (c.id == id && (type == 0 || c.type == type))
            return &c;
    return nullptr;
}

std::string text (std::vector<std::uint8_t> b)
{
    const auto z = std::find (b.begin(), b.end(), 0);
    std::string s (b.begin(), z);
    while (! s.empty() && (s.back() == ' ' || s.back() == '\r' || s.back() == '\n'))
        s.pop_back();
    return s;
}

std::string infoName (File& f, const std::vector<Ck>& kids)
{
    // (the INFO list's children are kept in a variable: `find` points into the list, so a temporary would be gone
    // before the name is read — rollup 0.1.30, macOS libc++ read freed memory there)
    if (const auto* info = find (kids, kList, kInfo))
    {
        const auto infoKids = f.children (*info);
        if (const auto* inam = find (infoKids, kInam))
            return text (f.body (*inam, 1024));
    }
    return {};
}

// ------------------------------------------------------------------------------------------------ the file
struct Gig
{
    File f;
    int major = 2;                              // 'vers' major (3: GigaStudio 3 / 4)
    std::vector<std::pair<std::uint32_t, std::uint32_t>> pool; // ptbl: (file number or high word, offset)
    std::vector<Ck> instruments;                // 'ins ' lists
    std::optional<Ck> wvpl;
    std::vector<std::string> extensionFiles;    // 'xfil'
    bool ok = false;

    Gig (const Access& a, const std::string& path) : f (a, path)
    {
        const auto top = f.chunkAt (0, f.length);
        if (! top || top->id != kRiff || top->type != kDls)
            return;
        for (const auto& c : f.children (*top))
        {
            if (c.id == kVers && c.size >= 8)
            {
                const auto b = f.body (c, 8);
                major = u16 (b, 2);
            }
            else if (c.id == kPtbl)
            {
                const auto b = f.body (c, (std::size_t) 64 << 20);
                const auto header = u32 (b, 0), count = u32 (b, 4);
                if (header < 8 || header > b.size() || count > (b.size() - header) / 4)
                    continue;
                const bool wide = (std::uint64_t) (c.size - header) == (std::uint64_t) count * 8;
                for (std::uint32_t i = 0; i < count; ++i)
                    pool.emplace_back (wide ? u32 (b, header + 8 * i) : 0u, wide ? u32 (b, header + 8 * i + 4) : u32 (b, header + 4 * i));
            }
            else if (c.id == kList && c.type == kLins)
            {
                for (const auto& i : f.children (c))
                    if (i.id == kList && i.type == kIns)
                        instruments.push_back (i);
            }
            else if (c.id == kList && (c.type == kWvpl || c.type == kDwpl))
                wvpl = c;
            else if (c.id == kXfil)
            {
                const auto b = f.body (c, 1 << 20);
                const auto n = std::min<std::uint32_t> (u32 (b, 0), 98);
                for (std::uint32_t i = 0; i < n && 4 + (i + 1) * 144 <= b.size(); ++i)
                    extensionFiles.push_back (text (std::vector<std::uint8_t> (b.begin() + 4 + i * 144, b.begin() + 4 + i * 144 + 128)));
            }
        }
        ok = true;
    }
};

// ------------------------------------------------------------------------------------------------ dimension regions
struct Dim
{
    std::uint8_t type = 0, bits = 0, zones = 0;
    int bitpos = 0, definition = 0;             // (definition: its place in '3lnk', which indexes the upper limits)
};

struct DimRegion
{
    bool hasWsmp = false;
    int unity = 60, fine = 0;
    std::int32_t gain = 0;                      // DLS attenuation units (1/655360 dB)
    bool loop = false;
    std::int64_t loopStart = 0, loopLength = 0;
    double attack = 0.0, decay1 = 0.005, release = 0.3;
    int sustain = 1000;                         // ‰
    bool infiniteSustain = true;
    int pan = 0;                                // −64 … 63
    bool pitchTrack = true;
    std::int64_t startOffset = 0;
    int velocityUpper = 0;                      // gig 2
    std::uint8_t upper[8] {};                   // gig 3: each dimension's upper limit
    bool filter = false;
    std::uint32_t wave = 0xFFFFFFFFu;           // wave pool index
};

void readWsmp (const std::vector<std::uint8_t>& b, DimRegion& d)
{
    if (b.size() < 20)
        return;
    d.hasWsmp = true;
    const auto header = u32 (b, 0);
    d.unity = u16 (b, 4);
    d.fine = i16 (b, 6);
    d.gain = i32 (b, 8);
    const auto loops = u32 (b, 16);
    if (loops > 0 && header >= 20 && header + 16 <= b.size())
    {
        d.loop = true;
        d.loopStart = u32 (b, header + 8);
        d.loopLength = u32 (b, header + 12);
    }
}

void read3ewa (const std::vector<std::uint8_t>& b, DimRegion& d)
{
    if (b.size() < 140)
        return;
    d.attack = timeOf (i32 (b, 28));
    d.decay1 = timeOf (i32 (b, 32));
    d.sustain = u16 (b, 38);
    d.release = timeOf (i32 (b, 40));
    d.infiniteSustain = i32 (b, 80) == 0x7fffffff;
    d.startOffset = (std::uint16_t) i16 (b, 104);
    d.pitchTrack = (b[108] & 1) == 0;
    const int pan = b[109];
    d.pan = pan < 64 ? pan : -(pan - 63);
    d.velocityUpper = (std::int8_t) b[124];
    d.filter = (b[132] & 0x80) != 0;
    if (b.size() >= 148)
        std::memcpy (d.upper, b.data() + 140, 8);
}

// ------------------------------------------------------------------------------------------------ samples
struct Wave
{
    std::string path;                           // written WAV ("" = not usable)
    std::int64_t frames = 0;
    int channels = 1, bits = 16;
    double rate = 44100.0;
    bool compressed = false, tooLarge = false, unsupported = false;
};

// The wave LIST of pool entry `index` (its header at `start` + offset; some writers count from the list's data)
std::optional<Ck> waveAt (File& f, std::uint64_t start, std::uint64_t end, std::uint64_t offset)
{
    for (const std::uint64_t at : { start + offset, start + offset - 12 })
        if (at >= start && at < end)
            if (auto c = f.chunkAt (at, end); c && c->id == kList && c->type == kWave)
                return c;
    return std::nullopt;
}

struct Reader
{
    Gig& g;
    const Access& a;
    Report& report;
    std::string base;                           // the instrument's name (for the WAV files)
    std::map<std::uint32_t, Wave> waves;
    std::map<std::uint32_t, std::unique_ptr<File>> extFiles;
    int compressed = 0, tooLarge = 0, unreadable = 0;

    File* poolFile (std::uint32_t fileNo, std::optional<Ck>& wvpl)
    {
        if (fileNo == 0)
        {
            wvpl = g.wvpl;
            return &g.f;
        }
        if (fileNo > g.extensionFiles.size())
            return nullptr;
        auto& slot = extFiles[fileNo];
        if (! slot)
        {
            const auto sep = g.f.path.find_last_of ("\\/");
            const auto folder = sep == std::string::npos ? std::string() : g.f.path.substr (0, sep + 1);
            slot = std::make_unique<File> (a, folder + g.extensionFiles[fileNo - 1]);
        }
        File& ef = *slot;
        const auto top = ef.chunkAt (0, ef.length);
        if (! top || top->id != kRiff)
            return nullptr;
        for (const auto& c : ef.children (*top))
            if (c.id == kList && (c.type == kWvpl || c.type == kDwpl))
                wvpl = c;
        return &ef;
    }

    const Wave& wave (std::uint32_t index, const DimRegion& d)
    {
        if (const auto it = waves.find (index); it != waves.end())
            return it->second;
        auto& w = waves[index];
        if (index >= g.pool.size())
        {
            w.unsupported = true;
            ++unreadable;
            return w;
        }
        const auto [hi, lo] = g.pool[index];
        std::optional<Ck> wvpl;
        File* file = nullptr;
        std::uint64_t offset = lo;
        if (g.f.length >> 31) // (files of 2 GB and more: 64-bit offsets, no extension files)
        {
            file = &g.f;
            wvpl = g.wvpl;
            offset |= (std::uint64_t) hi << 32;
        }
        else
            file = poolFile (hi, wvpl);
        if (file == nullptr || ! wvpl)
        {
            w.unsupported = true;
            ++unreadable;
            return w;
        }
        const auto wl = waveAt (*file, wvpl->data, wvpl->data + wvpl->size, offset);
        if (! wl)
        {
            w.unsupported = true;
            ++unreadable;
            return w;
        }
        const auto kids = file->children (*wl);
        const auto* fmt = find (kids, kFmt);
        const auto* data = find (kids, kData);
        if (find (kids, kEwav) != nullptr)
        {
            w.compressed = true;
            ++compressed;
            return w;
        }
        if (fmt == nullptr || data == nullptr)
        {
            w.unsupported = true;
            ++unreadable;
            return w;
        }
        const auto fb = file->body (*fmt, 64);
        const auto tag = u16 (fb, 0);
        w.channels = u16 (fb, 2);
        w.rate = (double) u32 (fb, 4);
        w.bits = u16 (fb, 14);
        // (the frame size follows from channels × bits, as GigaStudio readers compute it: some writers leave the block
        // alignment of stereo samples at 2)
        const auto block = w.channels * w.bits / 8;
        if (tag != 1 || w.channels < 1 || w.channels > 2 || (w.bits != 8 && w.bits != 16 && w.bits != 24) || ! (w.rate >= 1000.0 && w.rate <= 384000.0))
        {
            w.unsupported = true;
            ++unreadable;
            return w;
        }
        if (data->size > kMaxSampleBytes)
        {
            w.tooLarge = true;
            ++tooLarge;
            return w;
        }
        w.frames = (std::int64_t) (data->size / block);
        auto pcm = file->read (data->data, (std::size_t) data->size);
        if (pcm.size() != data->size)
        {
            w.unsupported = true;
            ++unreadable;
            return w;
        }
        if (w.bits == 8) // (WAV's 8-bit samples are unsigned, DLS's are signed)
            for (auto& s : pcm)
                s = (std::uint8_t) (s ^ 0x80);
        std::string name;
        if (const auto* info = find (kids, kList, kInfo))
        {
            const auto infoKids = file->children (*info); // (kept: `find` points into it)
            if (const auto* inam = find (infoKids, kInam))
                name = text (file->body (*inam, 256));
        }
        if (name.empty())
            name = base + " " + std::to_string (index + 1);
        const auto wav = wavFile (pcm.data(), pcm.size(), w.channels, w.bits, w.rate, d.loop ? d.loopStart : -1,
                                  d.loop ? d.loopStart + d.loopLength : -1, d.unity);
        pcm.clear();
        pcm.shrink_to_fit();
        if (! wav.empty() && a.saveWav)
            w.path = a.saveWav (wav, name);
        if (w.path.empty())
            ++unreadable;
        return w;
    }
};

const char* dimensionName (std::uint8_t type)
{
    switch (type)
    {
        case 0x83: return "aftertouch";
        case 0x84: return "release trigger";
        case 0x85: return "keyswitch";
        case 0x88: return "smart MIDI";
        case 0x01: return "mod wheel";
        case 0x02: return "breath";
        case 0x04: return "foot pedal";
        case 0x40: return "sustain pedal";
        case 0x41: return "portamento";
        case 0x42: return "sostenuto";
        case 0x43: return "soft pedal";
        default: return "MIDI controller";
    }
}
} // namespace

double timeOf (std::int32_t code)
{
    return std::pow (1.000000008813822, (double) code);
}

std::vector<std::string> instrumentNames (const std::string& path, const Access& access)
{
    Gig g (access, path);
    std::vector<std::string> names;
    if (! g.ok)
        return names;
    for (std::size_t i = 0; i < g.instruments.size(); ++i)
    {
        auto name = infoName (g.f, g.f.children (g.instruments[i]));
        names.push_back (name.empty() ? "Instrument " + std::to_string (i + 1) : name);
    }
    return names;
}

std::optional<Program> readInstrument (const std::string& path, const Access& access, int index, Report& report)
{
    Gig g (access, path);
    if (! g.ok)
    {
        report.error = "This is not a GigaStudio / DLS file LYNX SHARE can read.";
        return std::nullopt;
    }
    report.instruments = (int) g.instruments.size();
    if (index < 0 || index >= (int) g.instruments.size())
    {
        report.error = g.instruments.empty() ? "The file holds no instrument." : "There is no such instrument in the file.";
        return std::nullopt;
    }
    report.instrument = index;
    const auto insKids = g.f.children (g.instruments[(std::size_t) index]);
    Program program;
    program.name = infoName (g.f, insKids);
    if (program.name.empty())
        program.name = "Instrument " + std::to_string (index + 1);
    report.name = program.name;

    Reader reader { g, access, report, program.name, {}, {}, 0, 0, 0 };
    std::map<std::string, int> skippedDims; // dimension → dimension regions left out
    int filters = 0, decay2 = 0, nextGroup = 1, groupsShared = 0;
    const int dimBits = g.major > 2 ? 8 : 5;

    const auto* lrgn = find (insKids, kList, kLrgn);
    const auto regions = lrgn != nullptr ? g.f.children (*lrgn) : std::vector<Ck> {};
    for (const auto& rgn : regions)
    {
        if (rgn.id != kList || (rgn.type != kRgn && rgn.type != kRgn2))
            continue;
        const auto rk = g.f.children (rgn);
        int lowKey = 0, highKey = 127, lowVel = 0, highVel = 127, keyGroup = 0;
        if (const auto* h = find (rk, kRgnh))
        {
            const auto b = g.f.body (*h, 64);
            lowKey = u16 (b, 0), highKey = u16 (b, 2), lowVel = u16 (b, 4), highVel = u16 (b, 6), keyGroup = u16 (b, 10);
        }
        if (lowKey > 127 || lowKey > highKey)
            continue;
        highKey = std::min (highKey, 127);
        if (lowVel > highVel || lowVel > 127)
            lowVel = 0, highVel = 127;
        highVel = std::min (highVel, 127);

        // the dimension regions ('3prg' / '3ewl'), their parameters and samples ('3lnk')
        std::vector<DimRegion> drs;
        if (const auto* prg = find (rk, kList, k3prg))
            for (const auto& ewl : g.f.children (*prg))
                if (ewl.id == kList && ewl.type == k3ewl && drs.size() < 256)
                {
                    DimRegion d;
                    const auto ek = g.f.children (ewl);
                    if (const auto* w = find (ek, kWsmp))
                        readWsmp (g.f.body (*w, 4096), d);
                    if (const auto* e = find (ek, k3ewa))
                        read3ewa (g.f.body (*e, 4096), d);
                    drs.push_back (d);
                }
        std::vector<Dim> dims;
        if (const auto* lnk = find (rk, k3lnk))
        {
            const auto b = g.f.body (*lnk, 8192);
            const auto count = std::min<std::uint32_t> (u32 (b, 0), 256);
            int bitpos = 0;
            for (int i = 0; i < dimBits; ++i)
            {
                Dim d;
                d.type = u8 (b, 4 + 8 * (std::size_t) i);
                d.bits = u8 (b, 5 + 8 * (std::size_t) i);
                d.zones = u8 (b, 8 + 8 * (std::size_t) i);
                if (d.type == 0 || d.bits == 0)
                    continue;
                d.bits = std::min<std::uint8_t> (d.bits, 8);
                if (d.zones == 0 || d.zones > (1 << d.bits))
                    d.zones = (std::uint8_t) std::min (255, 1 << d.bits);
                d.bitpos = bitpos;
                d.definition = i;
                bitpos += d.bits;
                if (bitpos > 8)
                    break;
                dims.push_back (d);
            }
            const std::size_t table = g.major > 2 ? 68 : 44;
            for (std::uint32_t i = 0; i < count && i < drs.size(); ++i)
                drs[i].wave = u32 (b, table + 4 * i);
            if (bitpos > 8)
                continue;
        }
        else
        {
            // a plain DLS region: one dimension region from the region's own 'wsmp' / 'wlnk'
            DimRegion d;
            if (const auto* w = find (rk, kWsmp))
                readWsmp (g.f.body (*w, 4096), d);
            if (const auto* l = find (rk, kWlnk))
                d.wave = u32 (g.f.body (*l, 64), 8);
            drs.assign (1, d);
        }
        if (drs.empty())
            continue;

        // every combination of the dimensions' zones a plain note-on can reach
        int velDim = -1;
        for (std::size_t i = 0; i < dims.size(); ++i)
            if (dims[i].type == 0x82)
                velDim = (int) i;
        std::map<int, int> groupOf; // layer index → alternation group of this region
        std::vector<int> zone (dims.size(), 0);
        for (bool more = true; more;)
        {
            int idx = 0;
            for (std::size_t i = 0; i < dims.size(); ++i)
                idx |= zone[i] << dims[i].bitpos;
            bool plays = true;
            int layer = 0, alternation = 0;
            for (std::size_t i = 0; i < dims.size(); ++i)
            {
                const auto t = dims[i].type;
                if (t == 0x82 || zone[i] == 0)
                    continue;
                if (t == 0x81)
                    layer = zone[i];
                else if (t != 0x86 && t != 0x87 && t != 0x89)
                {
                    plays = false;
                    if (t != 0x80)
                        ++skippedDims[dimensionName (t)];
                }
            }
            for (const auto& d : dims)
                if (d.type == 0x86 || d.type == 0x89)
                    alternation = 1;
                else if (d.type == 0x87)
                    alternation = 2;
            if (plays && idx < (int) drs.size())
            {
                const auto& d = drs[(std::size_t) idx];
                Zone z;
                z.lowKey = lowKey, z.highKey = highKey, z.lowVel = lowVel, z.highVel = highVel;
                if (velDim >= 0)
                {
                    const auto& vd = dims[(std::size_t) velDim];
                    const auto k = zone[(std::size_t) velDim];
                    const int baseIdx = idx & ~(((1 << vd.bits) - 1) << vd.bitpos);
                    auto limit = [&] (int kk) -> int {
                        const auto& r = drs[std::min<std::size_t> ((std::size_t) (baseIdx | kk << vd.bitpos), drs.size() - 1)];
                        return g.major > 2 ? r.upper[vd.definition] : r.velocityUpper;
                    };
                    const auto& first = drs[std::min<std::size_t> ((std::size_t) baseIdx, drs.size() - 1)];
                    if ((g.major > 2 ? first.upper[vd.definition] : first.velocityUpper) != 0)
                    {
                        z.lowVel = k == 0 ? 0 : std::min (127, limit (k - 1) + 1);
                        z.highVel = k + 1 == vd.zones ? 127 : std::clamp (limit (k), z.lowVel, 127);
                    }
                    else
                    {
                        const int size = std::max (1, 128 / vd.zones);
                        z.lowVel = k * size;
                        z.highVel = k + 1 == vd.zones ? 127 : (k + 1) * size - 1;
                    }
                    z.lowVel = std::max (z.lowVel, lowVel);
                    z.highVel = std::min (z.highVel, highVel);
                    if (z.lowVel > z.highVel)
                        plays = false;
                }
                if (plays && d.wave != 0xFFFFFFFFu)
                {
                    const auto& w = reader.wave (d.wave, d);
                    if (! w.path.empty())
                    {
                        z.path = w.path;
                        z.name = fileNameOf (w.path);
                        z.root = std::clamp (d.unity, 0, 127);
                        z.fine = d.fine;
                        z.keyTrack = d.pitchTrack ? 100 : 0;
                        z.gainDb = -(double) d.gain / 655360.0;
                        z.pan = std::clamp (d.pan / 63.0, -1.0, 1.0);
                        z.frames = w.frames;
                        z.channels = w.channels;
                        z.bits = w.bits == 16 ? 16 : 32;
                        z.rate = w.rate;
                        z.offset = std::min<std::int64_t> (d.startOffset, std::max<std::int64_t> (0, w.frames - 1));
                        if (d.loop && d.loopLength > 0 && d.loopStart < w.frames)
                        {
                            z.loopMode = 2;
                            z.loopStart = d.loopStart;
                            z.loopEnd = std::min<std::int64_t> (w.frames, d.loopStart + d.loopLength);
                        }
                        z.attack = d.attack;
                        z.decay = d.decay1;
                        z.sustain = std::clamp (d.sustain / 1000.0, 0.0, 1.0);
                        z.release = d.release;
                        z.exclusiveGroup = std::clamp (keyGroup, 0, 255);
                        if (alternation != 0)
                        {
                            auto& gi = groupOf[layer];
                            if (gi == 0)
                            {
                                gi = nextGroup;
                                if (++nextGroup > 99)
                                    nextGroup = 1, ++groupsShared;
                                program.groups.push_back (Group { gi, alternation });
                            }
                            z.altGroup = gi;
                        }
                        filters += d.filter ? 1 : 0;
                        decay2 += d.infiniteSustain ? 0 : 1;
                        program.zones.push_back (std::move (z));
                    }
                }
            }
            // next combination
            more = false;
            for (std::size_t i = 0; i < dims.size(); ++i)
            {
                if (++zone[i] < dims[i].zones)
                {
                    more = true;
                    break;
                }
                zone[i] = 0;
            }
        }
    }

    auto& s = report.skipped;
    for (const auto& [name, count] : skippedDims)
        s.push_back (std::to_string (count) + " " + name + " dimension zone(s): only the zone a plain note plays was imported");
    if (reader.compressed > 0)
        s.push_back (std::to_string (reader.compressed) + " compressed sample(s) (GigaStudio's lossless compression is not read)");
    if (reader.tooLarge > 0)
        s.push_back (std::to_string (reader.tooLarge) + " sample(s) larger than 512 MB");
    if (reader.unreadable > 0)
        s.push_back (std::to_string (reader.unreadable) + " sample(s) that could not be read (missing extension file, unknown format)");
    if (filters > 0)
        s.push_back ("filters of " + std::to_string (filters) + " zone(s) (DirectWave's filters are left off)");
    if (decay2 > 0)
        s.push_back ("the second decay stage of " + std::to_string (decay2) + " zone(s) (DirectWave holds the sustain level)");
    if (groupsShared > 0)
        s.push_back ("more than 99 round-robin groups: some regions share an alternation counter");
    s.push_back ("not converted: LFOs, pitch and filter envelopes, velocity curves, crossfades, the instrument's scripts");
    if (program.zones.empty() && report.error.empty())
        report.error = "No zone of this instrument could be imported.";
    return program;
}
} // namespace lynx::msimport::gig
