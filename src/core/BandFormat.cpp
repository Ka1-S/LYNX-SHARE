#include "core/BandFormat.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <utility>

#include "core/XmlLite.h"

namespace lynx::band
{
namespace
{
using Bytes = std::vector<std::uint8_t>;

constexpr std::uint8_t magic[4] = { 0x23, 0x47, 0xC0, 0xAB };
constexpr std::size_t rootHeader = 24, recordHeader = 36;
constexpr std::int64_t regionOrigin = 34560; // placement positions: 34560 + tick (9 bars of 4/4 at 960 PPQ)
constexpr std::int64_t eventOrigin = 38400;  // note, tempo, signature and marker positions: 38400 + tick

std::uint16_t u16 (const std::uint8_t* p) noexcept { return (std::uint16_t) (p[0] | (p[1] << 8)); }
std::uint32_t u32 (const std::uint8_t* p) noexcept { return (std::uint32_t) p[0] | ((std::uint32_t) p[1] << 8) | ((std::uint32_t) p[2] << 16) | ((std::uint32_t) p[3] << 24); }

struct Record
{
    char tag[5] {};
    std::uint16_t classId = 0;
    std::uint32_t idx = 0, ordinal = 0;
    const std::uint8_t* raw = nullptr; // the header; the payload follows at +36
    std::size_t size = 0;              // payload bytes
    const std::uint8_t* pay() const noexcept { return raw + recordHeader; }
    bool is (const char* t) const noexcept { return std::memcmp (tag, t, 4) == 0; }
    std::uint32_t seq() const noexcept { return idx >> 18; }
};

bool hasMagic (const std::uint8_t* d, std::size_t n) noexcept { return n >= rootHeader && std::memcmp (d, magic, 4) == 0; }
bool modernHeader (const std::uint8_t* d, std::size_t n) noexcept { return hasMagic (d, n) && u32 (d + 0x10) == n - rootHeader; }

std::optional<std::vector<Record>> walk (const std::uint8_t* d, std::size_t n)
{
    if (! modernHeader (d, n))
        return std::nullopt;
    std::vector<Record> out;
    std::size_t at = rootHeader;
    while (at + recordHeader <= n)
    {
        Record r;
        std::memcpy (r.tag, d + at, 4);
        r.classId = u16 (d + at + 6);
        r.idx = u32 (d + at + 8);
        r.ordinal = u32 (d + at + 0x0C);
        r.size = u32 (d + at + 0x1C);
        r.raw = d + at;
        if (r.size > n - at - recordHeader)
            return std::nullopt;
        out.push_back (r);
        at += recordHeader + r.size;
    }
    if (at != n)
        return std::nullopt;
    return out;
}

// ---- property lists -------------------------------------------------------------------------------------------------
int base64Value (char c) noexcept
{
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 26;
    if (c >= '0' && c <= '9')
        return c - '0' + 52;
    if (c == '+')
        return 62;
    if (c == '/')
        return 63;
    return -1;
}

Bytes base64 (std::string_view text)
{
    Bytes out;
    out.reserve (text.size() * 3 / 4);
    std::uint32_t acc = 0;
    int bits = 0;
    for (const auto c : text)
    {
        const auto v = base64Value (c);
        if (v < 0)
            continue; // whitespace, padding
        acc = (acc << 6) | (std::uint32_t) v;
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            out.push_back ((std::uint8_t) ((acc >> bits) & 0xFF));
        }
    }
    return out;
}

std::optional<Plist> readXmlPlist (const Bytes& bytes)
{
    xml::Document doc;
    if (! doc.parse (std::string (bytes.begin(), bytes.end())))
        return std::nullopt;
    auto root = doc.root();
    if (! root || root.name() != "plist")
        return std::nullopt;
    Plist out;
    if (const auto dict = root.child ("dict"))
    {
        const auto kids = dict.children();
        for (std::size_t i = 0; i + 1 < kids.size(); ++i)
        {
            if (kids[i].name() != "key")
                continue;
            const auto& v = kids[i + 1];
            const auto type = v.name();
            if (type == "string" || type == "integer" || type == "real")
                out.values.emplace_back (kids[i].text(), v.text());
            else if (type == "true" || type == "false")
                out.values.emplace_back (kids[i].text(), std::string (type));
        }
    }
    std::function<void (const xml::Element&)> collect = [&] (const xml::Element& e) {
        for (const auto& c : e.children())
        {
            if (c.name() == "data")
                out.data.push_back (base64 (c.text()));
            else
                collect (c);
        }
    };
    collect (root);
    return out;
}

struct BinaryPlist
{
    const Bytes& b;
    std::size_t offsetSize = 0, refSize = 0, count = 0, top = 0, table = 0;

    std::uint64_t uint (std::size_t at, std::size_t size) const
    {
        std::uint64_t v = 0;
        for (std::size_t k = 0; k < size; ++k)
            v = (v << 8) | b[at + k];
        return v;
    }
    bool valid (std::size_t at, std::size_t size) const noexcept { return at <= b.size() && size <= b.size() - at; }
    std::optional<std::size_t> offsetOf (std::size_t object) const
    {
        if (object >= count || ! valid (table + object * offsetSize, offsetSize))
            return std::nullopt;
        const auto at = (std::size_t) uint (table + object * offsetSize, offsetSize);
        return at < table ? std::optional<std::size_t> (at) : std::nullopt;
    }
    // an object's marker, its length (or int size) and where its body starts
    bool header (std::size_t at, std::uint8_t& marker, std::size_t& length, std::size_t& body) const
    {
        if (! valid (at, 1))
            return false;
        marker = b[at];
        length = marker & 0x0F;
        body = at + 1;
        const auto type = marker >> 4;
        if (length == 0x0F && type != 0x0 && type != 0x1 && type != 0x2 && type != 0x3)
        {
            if (! valid (body, 1) || (b[body] >> 4) != 0x1)
                return false;
            const auto size = (std::size_t) 1 << (b[body] & 0x0F);
            if (size > 8 || ! valid (body + 1, size))
                return false;
            length = (std::size_t) uint (body + 1, size);
            body += 1 + size;
        }
        return true;
    }
    std::optional<std::string> scalar (std::size_t object) const
    {
        const auto at = offsetOf (object);
        std::uint8_t marker = 0;
        std::size_t length = 0, body = 0;
        if (! at || ! header (*at, marker, length, body))
            return std::nullopt;
        switch (marker >> 4)
        {
            case 0x0:
                if (marker == 0x08)
                    return std::string ("false");
                if (marker == 0x09)
                    return std::string ("true");
                return std::nullopt;
            case 0x1:
            {
                const auto size = (std::size_t) 1 << length;
                if (size > 8 || ! valid (body, size))
                    return std::nullopt;
                return std::to_string ((std::int64_t) uint (body, size));
            }
            case 0x2:
            {
                const auto size = (std::size_t) 1 << length;
                if (! valid (body, size))
                    return std::nullopt;
                double v = 0.0;
                if (size == 8)
                {
                    const auto bits = uint (body, 8);
                    std::memcpy (&v, &bits, 8);
                }
                else if (size == 4)
                {
                    const auto bits = (std::uint32_t) uint (body, 4);
                    float f = 0.0f;
                    std::memcpy (&f, &bits, 4);
                    v = f;
                }
                char text[64];
                std::snprintf (text, sizeof (text), "%.17g", v);
                return std::string (text);
            }
            case 0x5:
                if (! valid (body, length))
                    return std::nullopt;
                return std::string ((const char*) b.data() + body, length);
            case 0x6:
            {
                if (length > b.size() || ! valid (body, length * 2))
                    return std::nullopt;
                std::string out; // UTF-16 BE → UTF-8 (BMP)
                for (std::size_t k = 0; k < length; ++k)
                {
                    const auto c = (std::uint32_t) uint (body + 2 * k, 2);
                    if (c < 0x80)
                        out += (char) c;
                    else if (c < 0x800)
                    {
                        out += (char) (0xC0 | (c >> 6));
                        out += (char) (0x80 | (c & 0x3F));
                    }
                    else
                    {
                        out += (char) (0xE0 | (c >> 12));
                        out += (char) (0x80 | ((c >> 6) & 0x3F));
                        out += (char) (0x80 | (c & 0x3F));
                    }
                }
                return out;
            }
            default: return std::nullopt;
        }
    }
};

std::optional<Plist> readBinaryPlist (const Bytes& bytes)
{
    if (bytes.size() < 40 || std::memcmp (bytes.data(), "bplist00", 8) != 0)
        return std::nullopt;
    BinaryPlist p { bytes };
    const auto t = bytes.size() - 32;
    p.offsetSize = bytes[t + 6];
    p.refSize = bytes[t + 7];
    p.count = (std::size_t) p.uint (t + 8, 8);
    p.top = (std::size_t) p.uint (t + 16, 8);
    p.table = (std::size_t) p.uint (t + 24, 8);
    if (p.offsetSize < 1 || p.offsetSize > 8 || p.refSize < 1 || p.refSize > 8 || p.count == 0 || p.count > bytes.size()
        || p.table >= t || ! p.valid (p.table, p.count * p.offsetSize))
        return std::nullopt;
    Plist out;
    // every data object, in object order
    for (std::size_t k = 0; k < p.count; ++k)
    {
        const auto at = p.offsetOf (k);
        std::uint8_t marker = 0;
        std::size_t length = 0, body = 0;
        if (at && p.header (*at, marker, length, body) && (marker >> 4) == 0x4 && p.valid (body, length))
            out.data.emplace_back (bytes.begin() + (std::ptrdiff_t) body, bytes.begin() + (std::ptrdiff_t) (body + length));
    }
    // the top dictionary's scalars
    if (const auto at = p.offsetOf (p.top))
    {
        std::uint8_t marker = 0;
        std::size_t length = 0, body = 0;
        if (p.header (*at, marker, length, body) && (marker >> 4) == 0xD && length <= p.count && p.valid (body, 2 * length * p.refSize))
            for (std::size_t k = 0; k < length; ++k)
            {
                const auto key = p.scalar ((std::size_t) p.uint (body + k * p.refSize, p.refSize));
                const auto value = p.scalar ((std::size_t) p.uint (body + (length + k) * p.refSize, p.refSize));
                if (key && value)
                    out.values.emplace_back (*key, *value);
            }
    }
    return out;
}

// ---- song -----------------------------------------------------------------------------------------------------------
std::string text (const std::uint8_t* p, std::size_t n)
{
    std::string s ((const char*) p, n);
    while (! s.empty() && s.back() == '\0')
        s.pop_back();
    return s;
}

// A sequence's name (qeSM: u16 length @raw+0x34, bytes after it) and where the name ends (even), or nullopt
struct SeqName
{
    std::string name;
    std::size_t end = 0; // raw offset after the name, padded to even
};
std::optional<SeqName> seqName (const Record& r)
{
    if (r.size < 0x14)
        return std::nullopt;
    const auto n = (std::size_t) u16 (r.raw + 0x34);
    if (0x36 + n > recordHeader + r.size)
        return std::nullopt;
    return SeqName { text (r.raw + 0x36, n), 0x36 + n + (n & 1) };
}

bool isPlacement (const std::uint8_t* e, std::size_t left) noexcept
{
    return left >= 0x50 && (e[0] == 0x20 || e[0] == 0x24) && e[1] == 0 && e[2] == 0 && e[3] == 0 && u32 (e + 4) >= (std::uint32_t) regionOrigin
           && e[0x16] == 0 && e[0x17] == 0x89 && e[0x37] == 0x8A && u16 (e + 0x14) >= 1;
}

struct Placement
{
    bool audio = false;
    int track = 0; // 1-based arrange row
    std::int64_t start = 0;
    std::uint32_t child = 0xFFFFFFFF; // MIDI: the content sequence
    std::uint32_t regionK = 0, file = 0; // audio: gRuA (file, k), lFuA file
};

std::vector<Placement> placementsIn (const Record& r)
{
    std::vector<Placement> out;
    const auto* b = r.pay();
    std::size_t o = 0;
    while (o + 0x50 <= r.size)
    {
        if (! isPlacement (b + o, r.size - o))
        {
            o += 4;
            continue;
        }
        Placement p;
        p.audio = b[o] == 0x24;
        p.track = u16 (b + o + 0x14);
        p.start = (std::int64_t) u32 (b + o + 4) - regionOrigin;
        const auto child = u32 (b + o + 0x20);
        p.child = child == 0xFFFFFFFF ? child : child / 4;
        p.regionK = u32 (b + o + 0x28);
        p.file = u32 (b + o + 0x2C) / 4;
        out.push_back (p);
        o += 0x50;
    }
    return out;
}

// A region's events (§36 of the format notes): notes (32 bytes, extended notes + 16 per extension word), 16-byte
// controller / pressure / bend / lane events (extended ones + 16 per extension word, as GarageBand for iOS 2.3+ writes
// them: byte +1 = 0x40), a 32-byte region-settings event (0x32, its second word marked 0x88 @+0x17: GarageBand for iOS
// 2.3+, nothing to play), the 16-byte tail (F1). Anything else: content this reader can't play.
void readEvents (const Record& r, Region& region)
{
    const auto* b = r.pay();
    // the extension words after an extended event (byte +1 = 0x40): their byte +7 is 0xA3 / 0xA4 / 0xA7
    const auto extended = [b, &r] (std::size_t o, std::size_t size) {
        if (b[o + 1] == 0x40)
            while (o + size + 16 <= r.size && (b[o + size + 7] == 0xA3 || b[o + size + 7] == 0xA4 || b[o + size + 7] == 0xA7))
                size += 16;
        return size;
    };
    std::size_t o = 0;
    while (o + 16 <= r.size)
    {
        const auto status = b[o];
        if (status == 0xF1)
            return;
        if ((status & 0xF0) == 0x90)
        {
            if (o + 32 > r.size)
                return;
            Note n;
            n.tick = (std::int64_t) u32 (b + o + 4) - eventOrigin;
            n.velocity = std::clamp ((int) b[o + 0x0B], 1, 127);
            n.key = std::clamp ((int) b[o + 0x0C], 0, 127);
            n.length = std::max<std::int64_t> (1, (std::int64_t) u32 (b + o + 0x1C));
            if (n.tick < 0 || (region.length > 0 && n.tick >= region.length))
                ++region.hiddenNotes;
            else
                region.notes.push_back (n);
            o += extended (o, 32);
            continue;
        }
        const auto family = status & 0xF0;
        if (family == 0x80 || family == 0xA0 || family == 0xB0 || family == 0xD0 || family == 0xE0 || (status >= 0x50 && status <= 0x57))
        {
            ++region.controllerEvents;
            o += extended (o, 16);
            continue;
        }
        if (status == 0x32 && o + 32 <= r.size && b[o + 0x17] == 0x88)
        {
            o += 32;
            continue;
        }
        region.patternContent = true; // (Smart Drums and Drummer regions: their pattern events)
        return;
    }
}

bool genericStripName (const std::string& name, const char* prefix, int* number = nullptr)
{
    const auto n = std::strlen (prefix);
    if (name.size() <= n + 1 || name.compare (0, n, prefix) != 0 || name[n] != ' ')
        return false;
    int v = 0;
    for (std::size_t k = n + 1; k < name.size(); ++k)
    {
        if (name[k] < '0' || name[k] > '9')
            return false;
        v = v * 10 + (name[k] - '0');
    }
    if (number != nullptr)
        *number = v;
    return true;
}

bool systemStripName (const std::string& name)
{
    return genericStripName (name, "Aux") || genericStripName (name, "Bus") || name == "Output" || name == "Stereo Out" || name == "Master"
           || name == "Prelisten" || name == "Preview" || name == "Click";
}

std::string lowercase (std::string s)
{
    std::transform (s.begin(), s.end(), s.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
    return s;
}
} // namespace

std::string Plist::value (const std::string& key) const
{
    for (const auto& [k, v] : values)
        if (k == key)
            return v;
    return {};
}

std::optional<Plist> readPlist (const std::vector<std::uint8_t>& bytes)
{
    if (bytes.size() >= 8 && std::memcmp (bytes.data(), "bplist00", 8) == 0)
        return readBinaryPlist (bytes);
    return readXmlPlist (bytes);
}

std::optional<std::vector<ExsZone>> readExs (const std::vector<std::uint8_t>& bytes)
{
    constexpr std::size_t chunkHeader = 84;
    struct RawZone
    {
        ExsZone zone;
        std::uint32_t sample = 0;
    };
    std::vector<RawZone> zones;
    std::vector<std::string> samples;
    std::size_t at = 0;
    while (at + chunkHeader <= bytes.size())
    {
        const auto* c = bytes.data() + at;
        if (c[0] != 0x01 || std::memcmp (c + 16, "TBOS", 4) != 0)
            return std::nullopt; // (big-endian "SOBT" files and anything else)
        const auto size = (std::size_t) u32 (c + 4);
        if (size > bytes.size() - at - chunkHeader)
            return std::nullopt;
        const auto* d = c + chunkHeader;
        const auto type = c[3];
        if (type == 1 && size >= 0x60)
        {
            RawZone z;
            z.zone.rootKey = d[1] & 0x7F;
            z.zone.fineTune = (int) (std::int8_t) d[2];
            z.zone.lowKey = d[6] & 0x7F;
            z.zone.highKey = d[7] & 0x7F;
            z.zone.lowVelocity = d[9] & 0x7F;
            z.zone.highVelocity = d[10] & 0x7F;
            z.sample = u32 (d + 0x5C);
            zones.push_back (z);
        }
        else if (type == 3)
        {
            std::size_t n = 0;
            while (n < 64 && c[20 + n] != 0)
                ++n;
            samples.push_back (std::string ((const char*) c + 20, n));
        }
        at += chunkHeader + size;
    }
    if (at != bytes.size())
        return std::nullopt;
    std::vector<ExsZone> out;
    for (auto& z : zones)
        if (z.sample < samples.size() && ! samples[z.sample].empty())
        {
            z.zone.sample = samples[z.sample];
            out.push_back (z.zone);
        }
    return out;
}

int keyRootOf (const std::string& name) noexcept
{
    if (name.empty())
        return -1;
    static const int naturals[7] = { 9, 11, 0, 2, 4, 5, 7 }; // A B C D E F G
    const auto letter = (char) std::toupper ((unsigned char) name[0]);
    if (letter < 'A' || letter > 'G')
        return -1;
    auto root = naturals[letter - 'A'];
    for (std::size_t k = 1; k < name.size(); ++k)
    {
        if (name[k] == '#')
            ++root;
        else if (name[k] == 'b')
            --root;
        else
            break;
    }
    return ((root % 12) + 12) % 12;
}

bool isSongData (const std::uint8_t* data, std::size_t size) noexcept
{
    return walk (data, size).has_value();
}

std::optional<std::vector<std::uint8_t>> songFromProjectData (const std::vector<std::uint8_t>& file, std::string* error)
{
    auto fail = [error] (const char* why) -> std::optional<Bytes> {
        if (error != nullptr)
            *error = why;
        return std::nullopt;
    };
    const char* legacy = "a project from GarageBand 6–11 (or older): this layout is not readable yet; open and save it in "
                         "GarageBand 10 or GarageBand for iOS, or export its tracks";
    if (hasMagic (file.data(), file.size()))
    {
        if (! modernHeader (file.data(), file.size()))
            return fail (legacy);
        return file;
    }
    const auto plist = readPlist (file);
    if (! plist)
        return fail ("not a GarageBand project file");
    const Bytes* best = nullptr;
    for (const auto& d : plist->data)
        if (hasMagic (d.data(), d.size()) && (best == nullptr || d.size() > best->size()))
            best = &d;
    if (best == nullptr)
        return fail ("the GarageBand project holds no song");
    if (! modernHeader (best->data(), best->size()))
        return fail (legacy);
    return *best;
}

std::optional<Project> parseSong (const std::vector<std::uint8_t>& song, std::string* error)
{
    auto fail = [error] (const char* why) -> std::optional<Project> {
        if (error != nullptr)
            *error = why;
        return std::nullopt;
    };
    const auto walked = walk (song.data(), song.size());
    if (! walked)
        return fail (hasMagic (song.data(), song.size()) ? "a damaged or unknown GarageBand song layout" : "not a GarageBand song");
    const auto& records = *walked;

    Project p;
    {
        char hex[8];
        std::snprintf (hex, sizeof (hex), "%02x%02x", song[4], song[5]);
        p.version = hex;
    }

    // the song record: the initial tempo (u32 BPM × 10000 @raw+0x3A6) and the sample rate (u16 @payload+0x11E = rate − 15660)
    for (const auto& r : records)
        if (r.is ("gnoS"))
        {
            if (r.size + recordHeader >= 0x3AA)
                if (const auto t = u32 (r.raw + 0x3A6); t >= 100000 && t <= 9990000)
                    p.bpm = t / 10000.0;
            if (r.size >= 0x120)
                if (const auto rate = (int) u16 (r.pay() + 0x11E) + 15660; rate == 44100 || rate == 48000)
                    p.sampleRate = rate;
            break;
        }

    std::map<std::uint32_t, const Record*> seqNames, seqEvents; // content sequences (class 0x17)
    for (const auto& r : records)
    {
        if (r.classId != 0x17)
            continue;
        if (r.is ("qeSM"))
            seqNames.emplace (r.seq(), &r);
        else if (r.is ("qSvE"))
            seqEvents.emplace (r.seq(), &r);
    }

    // tempo map (class 3, events 0x60: u32 position, u32 BPM × 10000 @+0x10) and signature (class 1: an 80-byte header with
    // the first signature, den exponent @+0x0B and numerator @+0x0C, then 48-byte changes)
    for (const auto& r : records)
    {
        if (! r.is ("qSvE"))
            continue;
        const auto* b = r.pay();
        // (the song's own maps are in group 0; GarageBand also keeps a second tempo sequence in another group)
        if (r.classId == 3 && r.idx == 0 && r.size >= 32 && b[0] == 0x60)
        {
            for (std::size_t o = 0; o + 32 <= r.size && b[o] == 0x60; o += 32)
            {
                const auto tick = (std::int64_t) u32 (b + o + 4) - eventOrigin;
                const auto bpm = u32 (b + o + 0x10) / 10000.0;
                if (bpm <= 0.0)
                    continue;
                if (o == 0)
                    continue; // (the initial tempo: the song record's)
                if (std::abs (bpm - (p.tempoChanges.empty() ? p.bpm : p.tempoChanges.back().second)) > 1e-6)
                    p.tempoChanges.push_back ({ std::max<std::int64_t> (0, tick), bpm });
            }
        }
        else if (r.classId == 1 && r.idx == 0 && r.size >= 32 + 48 && b[0] == 0x32 && b[0x17] == 0x88)
        {
            // round 190mx (issue #184's file): GarageBand for iOS 2.3+ starts this sequence with the same 32-byte settings
            // event as its regions (0x32, marked 0x88 @+0x17), then the signatures as 48-byte events (0x30, den exponent
            // @+0x0B, numerator @+0x0C): the first is the song's, the others are changes (read before as 7/1)
            bool first = true;
            for (std::size_t o = 32; o + 48 <= r.size && b[o] == 0x30; o += 48)
            {
                const auto n = (int) b[o + 0x0C], e = (int) b[o + 0x0B];
                if (n < 1 || n > 64 || e < 0 || e > 5)
                    continue;
                if (std::exchange (first, false))
                {
                    p.numerator = n;
                    p.denominator = 1 << e;
                }
                else
                    p.meters.push_back ({ std::max<std::int64_t> (0, (std::int64_t) u32 (b + o + 4) - eventOrigin), { n, 1 << e } });
            }
        }
        else if (r.classId == 1 && r.idx == 0 && r.size >= 80 + 16)
        {
            const auto num = (int) b[0x0C], exp = (int) b[0x0B];
            if (num >= 1 && num <= 64 && exp >= 0 && exp <= 5)
            {
                p.numerator = num;
                p.denominator = 1 << exp;
            }
            for (std::size_t o = 80; o + 48 <= r.size && b[o] == 0x30; o += 48)
            {
                const auto n = (int) b[o + 0x0C], e = (int) b[o + 0x0B];
                if (n >= 1 && n <= 64 && e >= 0 && e <= 5)
                    p.meters.push_back ({ std::max<std::int64_t> (0, (std::int64_t) u32 (b + o + 4) - eventOrigin), { n, 1 << e } });
            }
        }
        else if (r.classId == 0x16 && r.size >= 48 && b[0] == 0x12)
        {
            // markers: 48-byte events (position, link id @+0x10); names in the RTF text records keyed by that id
            std::map<std::uint16_t, std::string> names;
            for (const auto& t : records)
            {
                if (! t.is ("qSxT"))
                    continue;
                const std::string body ((const char*) t.raw, t.size + recordHeader);
                const auto at = body.find ("\\cf2 ");
                if (at == std::string::npos)
                    continue;
                const auto end = body.find ('}', at);
                auto name = body.substr (at + 5, end == std::string::npos ? std::string::npos : end - at - 5);
                while (! name.empty() && (name.back() == '\n' || name.back() == ' '))
                    name.pop_back();
                names[u16 (t.raw + 0x0A)] = name;
            }
            for (std::size_t o = 0; o + 48 <= r.size && b[o] == 0x12; o += 48)
                p.markers.push_back ({ std::max<std::int64_t> (0, (std::int64_t) u32 (b + o + 4) - eventOrigin),
                                       names[(std::uint16_t) u32 (b + o + 0x10)] });
        }
    }

    // the arrangement's sequence: the one holding the most region placements outside the automation holders
    std::map<std::uint32_t, std::vector<Placement>> bySeq;
    for (const auto& r : records)
        if (r.is ("qSvE") && r.classId == 0x17)
            for (const auto& pl : placementsIn (r))
                bySeq[r.seq()].push_back (pl);
    const auto automation = [&seqNames] (std::uint32_t child) {
        const auto it = seqNames.find (child);
        if (it == seqNames.end())
            return false;
        const auto n = seqName (*it->second);
        return n && ! n->name.empty() && n->name[0] == '*';
    };
    std::uint32_t root = 0;
    std::size_t rootCount = 0;
    for (const auto& [seq, list] : bySeq)
    {
        const auto count = (std::size_t) std::count_if (list.begin(), list.end(), [&] (const Placement& pl) { return pl.audio || ! automation (pl.child); });
        if (count > rootCount)
        {
            root = seq;
            rootCount = count;
        }
    }

    // arrange rows (karT idx 0x40000, populated): row order, channel slot (raw+0x2C), mute (raw+0x27 bit 0)
    struct Row
    {
        int row;
        std::uint32_t slot;
        bool muted;
        int rowType;
    };
    std::vector<Row> rows;
    for (const auto& r : records)
        if (r.is ("karT") && r.idx == 0x40000 && u16 (r.raw + 0x14) == 0 && r.size >= 0x0C)
            rows.push_back ({ (int) u16 (r.raw + 0x12), u32 (r.raw + 0x2C), (r.raw[0x27] & 1) != 0, (int) r.raw[0x24] });
    std::sort (rows.begin(), rows.end(), [] (const Row& a, const Row& b) { return a.row < b.row; });

    // environment objects: channel-strip names by slot (u16 length @pay+0x9E, name @pay+0xA0)
    std::map<std::uint32_t, std::string> stripNames;
    for (const auto& r : records)
        if (r.is ("ivnE") && r.size > 0xA0)
            if (const auto n = (std::size_t) u16 (r.pay() + 0x9E); 0xA0 + n <= r.size)
                stripNames[r.idx >> 16] = text (r.pay() + 0xA0, n);
    // mixer strips (OCuA, full strips: magic 0x1235 @pay+0x0A): name (16 bytes @pay+0x3C), fader @+0x55, pan @+0x59
    std::map<std::string, std::pair<int, int>> strips;
    std::map<std::string, std::uint32_t> stripNumbers; // name → the strip's number (ordinal >> 16)
    for (const auto& r : records)
        if (r.is ("OCuA") && r.size > 0x78 && u16 (r.pay() + 0x0A) == 0x1235 && r.pay()[4] != 0xFF)
        {
            auto name = text (r.pay() + 0x3C, 16);
            const auto first = name.find_first_not_of (" \xA5");
            name = first == std::string::npos ? std::string() : name.substr (first);
            strips.emplace (name, std::make_pair ((int) r.pay()[0x55], (int) r.pay()[0x59]));
            stripNumbers.emplace (name, r.ordinal >> 16);
        }
    // a strip's plugin records (UCuA, the strip's ordinal, magic 0x1235 @pay+0x0A; its slot u16 @raw+0x12). Found on
    // GarageBand for iOS 2.3+ files (issue #184's project and Astronomia; older files keep other bytes there, which the
    // checks below refuse): slot 2 = the instrument, its setting file @pay+0x0E ("Ring.exs" for a Sampler) and the plugin's
    // name @raw+0x9C ("Sampler", "Alchemy", "Drum Kits"); slot 0x10 = the patch, its file @pay+0x10 ("808 Flex.patch")
    // and its category @pay+0x50 ("Drums")
    struct Plugins
    {
        std::string setting, unit, patch, category;
    };
    std::map<std::uint32_t, Plugins> pluginsOf;
    {
        // a NUL-terminated printable name of at most `max` bytes, else empty
        const auto name = [] (const std::uint8_t* p, std::size_t max) {
            std::size_t n = 0;
            while (n < max && p[n] != 0)
            {
                if (p[n] < 0x20 || p[n] == 0x7F)
                    return std::string();
                ++n;
            }
            return n < max ? std::string ((const char*) p, n) : std::string();
        };
        const auto endsWith = [] (const std::string& s, const char* tail) {
            const auto n = std::strlen (tail);
            return s.size() > n && s.compare (s.size() - n, n, tail) == 0;
        };
        for (const auto& r : records)
        {
            if (! r.is ("UCuA") || r.size < 0x90 || u16 (r.pay() + 0x0A) != 0x1235)
                continue;
            auto& plugins = pluginsOf[r.ordinal >> 16];
            const auto slot = u16 (r.raw + 0x12);
            if (slot == 2 && r.size >= 0x9C + 32 - recordHeader)
            {
                if (const auto setting = name (r.pay() + 0x0E, 64); endsWith (setting, ".exs"))
                    plugins.setting = setting;
                plugins.unit = name (r.raw + 0x9C, 32);
            }
            else if (slot == 0x10)
            {
                auto patch = name (r.pay() + 0x10, 64);
                for (const auto* ext : { ".cst", ".patch" })
                    if (endsWith (patch, ext))
                    {
                        plugins.patch = patch.substr (0, patch.size() - std::strlen (ext));
                        plugins.category = name (r.pay() + 0x50, 32);
                    }
            }
        }
    }

    // audio regions (gRuA by (file, region number)) and files (lFuA: UTF-16 name, u16 count @pay+0x08)
    std::map<std::pair<std::uint32_t, std::uint32_t>, const Record*> audioRegions;
    std::map<std::uint32_t, std::string> files;
    for (const auto& r : records)
    {
        if (r.idx % 0x40000 != 0)
            continue;
        if (r.is ("gRuA") && r.ordinal % 0x10000 == 0 && r.size >= 0x4C)
            audioRegions.emplace (std::make_pair (r.idx / 0x40000, r.ordinal >> 16), &r);
        else if (r.is ("lFuA") && r.size >= 0x0A && ! files.count (r.idx / 0x40000))
        {
            const auto n = (std::size_t) u16 (r.pay() + 8);
            if (0x0A + 2 * n > r.size)
                continue;
            std::string name;
            for (std::size_t k = 0; k < n; ++k)
            {
                auto c = (std::uint32_t) u16 (r.pay() + 0x0A + 2 * k);
                if (c >= 0xD800 && c <= 0xDBFF && k + 1 < n)
                {
                    const auto lo = (std::uint32_t) u16 (r.pay() + 0x0A + 2 * (k + 1));
                    c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
                    ++k;
                }
                if (c < 0x80)
                    name += (char) c;
                else if (c < 0x800)
                {
                    name += (char) (0xC0 | (c >> 6));
                    name += (char) (0x80 | (c & 0x3F));
                }
                else if (c < 0x10000)
                {
                    name += (char) (0xE0 | (c >> 12));
                    name += (char) (0x80 | ((c >> 6) & 0x3F));
                    name += (char) (0x80 | (c & 0x3F));
                }
                else
                {
                    name += (char) (0xF0 | (c >> 18));
                    name += (char) (0x80 | ((c >> 12) & 0x3F));
                    name += (char) (0x80 | ((c >> 6) & 0x3F));
                    name += (char) (0x80 | (c & 0x3F));
                }
            }
            files[r.idx / 0x40000] = name;
        }
    }

    const auto& placed = bySeq[root];
    // the tracks, in arrange order; rows of the Output / Aux / Master strips are not tracks
    std::vector<std::pair<const Row*, Track>> tracks;
    for (const auto& row : rows)
    {
        const auto stripName = stripNames.count (row.slot) ? stripNames[row.slot] : std::string();
        const auto any = std::any_of (placed.begin(), placed.end(), [&] (const Placement& pl) { return pl.track == row.row + 1; });
        if (row.rowType == 3 || (! any && systemStripName (stripName)))
            continue;
        Track t;
        t.muted = row.muted;
        for (const auto& pl : placed)
        {
            if (pl.track != row.row + 1)
                continue;
            Region region;
            region.audio = pl.audio;
            region.start = std::max<std::int64_t> (0, pl.start);
            if (pl.audio)
            {
                t.audio = true;
                if (const auto f = files.find (pl.file); f != files.end())
                    region.file = f->second;
                auto g = audioRegions.find ({ pl.file, pl.regionK });
                if (g == audioRegions.end())
                    g = audioRegions.find ({ pl.file, 0 });
                if (g == audioRegions.end() || region.file.empty())
                    continue;
                const auto& rec = *g->second;
                const auto* pay = rec.pay();
                region.trimSamples = u32 (pay + 0x06);
                region.lengthSamples = u32 (pay + 0x16);
                const auto n = (std::size_t) u16 (pay + 0x4A);
                if (0x4C + n <= rec.size)
                    region.name = text (pay + 0x4C, n);
                const std::string_view raw ((const char*) rec.raw, rec.size + recordHeader);
                region.fades = raw.find ("EDAF") != std::string_view::npos;
                if (region.lengthSamples <= 0)
                    continue;
            }
            else
            {
                if (automation (pl.child))
                    continue;
                const auto names = seqNames.find (pl.child);
                if (names == seqNames.end())
                    continue;
                const auto n = seqName (*names->second);
                if (! n || n->end + 0x40 > names->second->size + recordHeader)
                    continue;
                region.name = n->name;
                region.length = (std::int64_t) u32 (names->second->raw + n->end + 0x3C);
                if (bySeq.count (pl.child) && ! bySeq[pl.child].empty())
                    region.folder = true; // a folder / take folder: a sequence of regions
                else if (const auto events = seqEvents.find (pl.child); events != seqEvents.end())
                    readEvents (*events->second, region);
                if (region.length <= 0)
                {
                    std::int64_t end = 0;
                    for (const auto& note : region.notes)
                        end = std::max (end, note.tick + note.length);
                    region.length = std::max<std::int64_t> (end, ppq);
                }
            }
            t.regions.push_back (std::move (region));
        }
        std::sort (t.regions.begin(), t.regions.end(), [] (const Region& a, const Region& b) { return a.start < b.start; });
        // the name: the strip's when the user named it, else the first region's (GarageBand names regions after the
        // instrument patch or "Audio Recorder"), else a numbered default
        if (! stripName.empty() && ! genericStripName (stripName, "Inst") && ! genericStripName (stripName, "Audio") && ! systemStripName (stripName))
            t.name = stripName;
        else if (! t.regions.empty() && ! t.regions.front().name.empty())
        {
            t.name = t.regions.front().name;
            if (t.regions.front().audio)
                while (t.name.size() > 2 && t.name[t.name.size() - 2] == '.' && std::isdigit ((unsigned char) t.name.back()))
                    t.name.resize (t.name.size() - 2); // "Audio Recorder.2" → "Audio Recorder"
        }
        if (! t.audio && t.regions.empty() && genericStripName (stripName, "Audio"))
            t.audio = true;
        tracks.push_back ({ &row, std::move (t) });
    }

    // each track's fader and pan: its channel strip. The strip of a row named "Inst N" / "Audio N" is the mixer strip of
    // that name; a strip the user renamed (GarageBand 10 for Mac) takes its number by order: strips of one kind are
    // numbered in slot order
    std::map<std::uint32_t, std::string> stripOf;
    {
        std::map<std::uint32_t, bool> kindOfSlot; // slot → audio
        for (const auto& [row, t] : tracks)
            kindOfSlot[row->slot] = t.audio;
        for (const auto& [slot, name] : stripNames)
        {
            if (genericStripName (name, "Inst"))
                kindOfSlot.emplace (slot, false);
            else if (genericStripName (name, "Audio"))
                kindOfSlot.emplace (slot, true);
        }
        int instCount = 0, audioCount = 0;
        for (const auto& [slot, audio] : kindOfSlot)
        {
            const auto& name = stripNames[slot];
            int number = 0;
            auto& counter = audio ? audioCount : instCount;
            if (genericStripName (name, audio ? "Audio" : "Inst", &number))
                counter = number;
            else
                ++counter;
            stripOf[slot] = std::string (audio ? "Audio " : "Inst ") + std::to_string (counter);
        }
    }
    int instruments = 0, audios = 0;
    for (auto& [row, t] : tracks)
    {
        if (const auto s = stripOf.find (row->slot); s != stripOf.end())
        {
            if (const auto m = strips.find (s->second); m != strips.end())
            {
                t.volume = m->second.first;
                t.pan = m->second.second;
                t.volumeKnown = true;
            }
            if (const auto n = stripNumbers.find (s->second); n != stripNumbers.end())
                if (const auto pl = pluginsOf.find (n->second); pl != pluginsOf.end() && ! t.audio)
                {
                    t.patch = pl->second.patch;
                    t.category = pl->second.category;
                    t.unit = pl->second.unit;
                    if (t.unit == "Sampler" || t.unit == "EXS24")
                        t.samplerInstrument = pl->second.setting;
                }
        }
        if (t.name.empty() && ! t.patch.empty())
            t.name = t.patch; // (a track without regions or a name of its own: GarageBand shows its patch's name)
        if (t.name.empty())
            t.name = t.audio ? "Audio " + std::to_string (++audios) : "Instrument " + std::to_string (++instruments);
        p.tracks.push_back (std::move (t));
    }
    return p;
}

void applyMetadata (const std::vector<std::uint8_t>& bytes, Project& project)
{
    const auto plist = readPlist (bytes);
    if (! plist)
        return;
    const auto number = [&plist] (const char* key, double& out) {
        const auto v = plist->value (key);
        if (v.empty())
            return false;
        char* end = nullptr;
        const auto d = std::strtod (v.c_str(), &end);
        if (end == v.c_str() || ! std::isfinite (d))
            return false;
        out = d;
        return true;
    };
    const auto first = [&plist] (std::initializer_list<const char*> keys) {
        for (const auto* k : keys)
            if (auto v = plist->value (k); ! v.empty())
                return v;
        return std::string();
    };
    if (auto title = first ({ "com_apple_garageband_metadata_songTitle" }); ! title.empty())
        project.title = title;
    double num = 0.0, den = 0.0;
    if ((number ("com_apple_garageband_metadata_songSignatureNominator", num) || number ("SongSignatureNumerator", num))
        && (number ("com_apple_garageband_metadata_songSignatureDeNominator", den) || number ("SongSignatureDenominator", den))
        && num >= 1 && num <= 64 && (den == 1 || den == 2 || den == 4 || den == 8 || den == 16 || den == 32))
    {
        project.numerator = (int) num;
        project.denominator = (int) den;
    }
    if (double rate = 0.0; number ("SampleRate", rate) && rate >= 8000.0 && rate <= 384000.0)
        project.sampleRate = rate;
    const auto key = first ({ "com_apple_garageband_metadata_songKey", "SongKey" });
    project.keyRoot = keyRootOf (key);
    project.keyMinor = lowercase (first ({ "com_apple_garageband_metadata_songGender", "SongGenderKey" })) == "minor";
}
} // namespace lynx::band
