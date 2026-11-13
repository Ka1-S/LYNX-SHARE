#include "core/FlmFormat.h"

#include <algorithm>
#include <cmath>
#include <cstring>

// Layout: docs/analise/FLM_FORMAT.md (round 182c). Every read is bounds-checked; a nested container that runs short
// keeps what it read so far (the app's own reader does the same), only a top-level chunk past the end of the file fails.

namespace lynx::flm
{
namespace
{
using Bytes = std::span<const std::uint8_t>;

// little-endian field reader over one chunk body; reads past the end return the fallback and set `ok` = false
struct Reader
{
    Bytes data;
    std::size_t pos = 0;
    bool ok = true;

    bool has (std::size_t n) const noexcept { return pos + n <= data.size(); }
    std::size_t left() const noexcept { return pos < data.size() ? data.size() - pos : 0; }

    template <typename T>
    T raw (T fallback = {}) noexcept
    {
        if (! has (sizeof (T)))
        {
            ok = false;
            pos = data.size();
            return fallback;
        }
        T v;
        std::memcpy (&v, data.data() + pos, sizeof (T)); // (the targets are little-endian, as the files)
        pos += sizeof (T);
        return v;
    }
    std::uint8_t u8 (std::uint8_t f = 0) noexcept { return raw<std::uint8_t> (f); }
    std::uint16_t u16 (std::uint16_t f = 0) noexcept { return raw<std::uint16_t> (f); }
    std::uint32_t u32 (std::uint32_t f = 0) noexcept { return raw<std::uint32_t> (f); }
    std::int32_t i32 (std::int32_t f = 0) noexcept { return raw<std::int32_t> (f); }
    float f32 (float f = 0) noexcept { return raw<float> (f); }
    double f64 (double f = 0) noexcept { return raw<double> (f); }

    // a NUL-padded text field of `size` bytes
    std::string fixed (std::size_t size)
    {
        if (! has (size))
        {
            ok = false;
            pos = data.size();
            return {};
        }
        const auto* p = reinterpret_cast<const char*> (data.data() + pos);
        pos += size;
        const auto* end = static_cast<const char*> (std::memchr (p, 0, size));
        return std::string (p, end != nullptr ? (std::size_t) (end - p) : size);
    }
    std::string counted (std::size_t length)
    {
        length = std::min (length, left());
        std::string s (reinterpret_cast<const char*> (data.data() + pos), length);
        pos += length;
        return s;
    }
    std::string lp32()
    {
        if (! has (4))
        {
            ok = false;
            return {};
        }
        return counted (u32());
    }
    Bytes rest() const noexcept { return pos < data.size() ? data.subspan (pos) : Bytes {}; }
};

bool tagChar (std::uint8_t c) noexcept { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; }

bool isTag (Bytes b, std::size_t at) noexcept
{
    return at + 4 <= b.size() && tagChar (b[at]) && tagChar (b[at + 1]) && tagChar (b[at + 2]) && tagChar (b[at + 3]);
}

std::string tagOf (Bytes b, std::size_t at) { return std::string (reinterpret_cast<const char*> (b.data() + at), 4); }

struct Chunk
{
    std::string tag;
    Bytes body;
};

// the chunks of a container body from `at`; stops at padding, a bad tag or a chunk past the end (`overrun` set)
std::vector<Chunk> chunksOf (Bytes b, std::size_t at, bool* overrun = nullptr)
{
    std::vector<Chunk> out;
    while (at + 8 <= b.size() && isTag (b, at))
    {
        std::uint32_t size;
        std::memcpy (&size, b.data() + at + 4, 4);
        if ((std::size_t) size > b.size() - at - 8)
        {
            if (overrun != nullptr)
                *overrun = true;
            break;
        }
        out.push_back ({ tagOf (b, at), b.subspan (at + 8, size) });
        at += 8 + (std::size_t) size;
    }
    return out;
}

// a versioned container magic: two digits + two letters ("10KR", "20HC")
bool isMagic (Bytes b, std::size_t at, const char* letters) noexcept
{
    return at + 4 <= b.size() && b[at] >= '0' && b[at] <= '9' && b[at + 1] >= '0' && b[at + 1] <= '9' && b[at + 2] == (std::uint8_t) letters[0]
           && b[at + 3] == (std::uint8_t) letters[1];
}

std::vector<float> floatsOf (Bytes b)
{
    std::vector<float> v (b.size() / 4);
    if (! v.empty())
        std::memcpy (v.data(), b.data(), v.size() * 4);
    return v;
}

std::string slashes (std::string s)
{
    std::replace (s.begin(), s.end(), '\\', '/');
    return s;
}

SampleRef readSample (Bytes body)
{
    SampleRef s;
    const std::size_t start = isMagic (body, 0, "LS") ? 4 : 0;
    bool haveStr1 = false;
    for (const auto& c : chunksOf (body, start))
    {
        Reader r { c.body };
        if (c.tag == "MAIN")
        {
            r.u32();
            r.f64();
            s.name = r.fixed (512);
            r.fixed (512); // folder
            if (r.has (15))
            {
                s.reverse = r.u8() != 0;
                const auto shift = r.f64();
                r.f32();
                s.cutGroup = r.u8();
                s.polyphonic = r.u8() != 0;
                s.timeShift = std::isfinite (shift) ? shift : 0.0;
            }
        }
        else if (c.tag == "LINk")
        {
            s.slotId = r.i32 (-1);
            r.i32 (-1);
            r.u16();
            s.outputChannel = r.has (4) ? r.i32 (-1) : -1;
        }
        else if (c.tag == "PATH") // the legacy fixed path (FLM still reads it; drum templates and old kits)
        {
            if (auto path = r.fixed (2048); s.savedPath.empty() && ! path.empty())
                s.savedPath = std::move (path);
        }
        else if (c.tag == "STRC") // the legacy stretch: mode, ratio, pitch as a ratio (STR1 wins when both are there)
        {
            const auto mode = r.u8();
            const auto stretch = r.f64 (1.0);
            const auto ratio = r.f64 (1.0);
            if (r.ok && ! haveStr1)
            {
                s.stretchMode = mode;
                s.stretch = stretch;
                s.pitch = ratio > 0.0 && std::isfinite (ratio) ? 12.0 * std::log2 (ratio) : 0.0;
                s.legacyStretch = true;
            }
        }
        else if (c.tag == "PTH1")
        {
            const auto n = r.u16();
            const auto text = r.counted (n);
            const auto nul = text.find ('\0');
            if (nul != std::string::npos)
            {
                s.root = text.substr (0, nul);
                s.path = slashes (text.substr (nul + 1));
            }
            else
                s.path = slashes (text);
        }
        else if (c.tag == "PRST")
        {
            s.savedPath = r.lp32();
            s.relName = slashes (r.lp32());
        }
        else if (c.tag == "STR1")
        {
            const auto mode1 = r.u8();
            const auto stretch = r.f64 (1.0);
            const auto pitch = r.f64 (0.0);
            if (r.ok)
            {
                s.stretchMode = mode1;
                s.stretch = stretch;
                s.pitch = pitch;
                s.legacyStretch = false;
                haveStr1 = true;
            }
        }
        else if (c.tag == "PRMS")
            s.params = floatsOf (c.body);
    }
    return s;
}

Module readModule (Bytes body)
{
    Module m;
    Reader head { body };
    m.type = head.u32();
    m.instanceId = head.u32();
    for (const auto& c : chunksOf (body, 8))
    {
        Reader r { c.body };
        if (c.tag == "ADD1")
        {
            m.bypassFlag = r.u8() != 0;
            const auto lo = r.u8 (0), hi = r.u8 (127);
            m.keyLow = std::min<int> (std::min (lo, hi), 127);
            m.keyHigh = std::min<int> (std::max (lo, hi), 127);
        }
        else if (c.tag == "DESC")
        {
            m.descName = r.fixed (256);
            m.descFolder = r.fixed (256);
        }
        else if (c.tag == "PRST")
        {
            m.presetFile = r.lp32();
            m.presetName = slashes (r.lp32());
        }
        else if (c.tag == "PRMS")
            m.params = floatsOf (c.body);
        else if (c.tag == "SMPR")
            m.reserved = floatsOf (c.body);
        else if (c.tag == "CSTM")
            m.custom.assign (c.body.begin(), c.body.end());
    }
    return m;
}

Rack readRack (Bytes body)
{
    Rack rack;
    Reader r { body };
    rack.kind = r.u32();
    if (! isMagic (body, 4, "KR"))
        return rack;
    for (const auto& c : chunksOf (body, 8))
    {
        if (c.tag == "RPRM")
            rack.params = floatsOf (c.body);
        else if (c.tag == "RSMP")
        {
            const std::size_t start = isMagic (c.body, 0, "MS") ? 8 : 0; // magic + u32
            for (const auto& s : chunksOf (c.body, start))
            {
                if (s.tag == "PADS")
                    rack.pads.assign (s.body.begin(), s.body.end());
                else if (s.tag == "SMPl" || s.tag == "SMPL")
                    rack.kit.push_back (readSample (s.body));
            }
        }
        else if (c.tag == "RMOd" || c.tag == "RMOD")
            rack.modules.push_back (readModule (c.body));
        else if (c.tag == "CCLK") // round 190dw item 4: (cc, module instance, parameter) triples
        {
            Reader f { c.body };
            while (f.has (12))
            {
                Rack::CcLink link;
                link.cc = f.i32();
                link.module = f.i32();
                link.param = f.i32();
                rack.ccLinks.push_back (link);
            }
        }
    }
    return rack;
}

Clip readClip (Bytes body)
{
    Clip clip;
    Reader r { body };
    clip.startTick = r.u32();
    const std::size_t start = isMagic (body, 4, "LC") ? 8 : 4;
    for (const auto& c : chunksOf (body, start))
    {
        Reader f { c.body };
        if (c.tag == "CLHd")
        {
            clip.length = f.f64();
            clip.loopLength = f.f64();
            clip.offset = f.f64();
            clip.contentId = f.u32();
            clip.sourceLength = f.f64();
            clip.flag = f.u8();
        }
        else if (c.tag == "EVN2")
        {
            const auto recordSize = (std::size_t) f.u16();
            if (recordSize < 18)
                continue;
            while (f.has (recordSize))
            {
                Reader e { c.body.subspan (f.pos, recordSize) };
                f.pos += recordSize;
                Event ev;
                ev.tick = e.u32();
                ev.length = e.f64();
                ev.key = (std::int16_t) e.u16();
                ev.velocity = e.u8 (200);
                ev.pan = e.u8 (127);
                ev.value = e.u16 (0x7FFF);
                ev.repeat = (std::int8_t) e.u8();
                ev.slide = e.u8();
                clip.events.push_back (ev);
            }
        }
        else if (c.tag == "CLSm")
            clip.sample = readSample (c.body);
    }
    return clip;
}

Lane readLane (Bytes body)
{
    Lane lane;
    for (const auto& c : chunksOf (body, 0))
    {
        if (c.tag == "DESc")
        {
            Reader r { c.body };
            lane.rawKind = r.u32();
            lane.targetModule = r.i32 (-1);
            lane.targetParam = r.i32 (-1);
            lane.swing = r.f64();
            if (! std::isfinite (lane.swing))
                lane.swing = 0.0;
            r.u32();
            r.u32();
            lane.name = r.fixed (1024);
        }
        else if (c.tag == "CLIP")
            lane.clips.push_back (readClip (c.body));
    }
    return lane;
}

Channel readChannel (Bytes body)
{
    Channel ch;
    Reader r { body };
    ch.kind = r.u32();
    if (! isMagic (body, 4, "HC"))
        return ch;
    for (const auto& c : chunksOf (body, 8))
    {
        Reader f { c.body };
        if (c.tag == "CHHD")
        {
            ch.name = f.fixed (1024);
            f.u32();
            ch.order = f.f64 (ch.order);
            if (! f.has (1084 - f.pos))
                continue; // (an older channel header ends here)
            f.f32();
            f.f64();
            f.f64();
            f.u32();
            f.u32();
            f.pos += 16; // the channel's GUID
            ch.channelId = f.i32 (-1);
            ch.typeName = f.lp32();
            ch.userName = f.lp32();
        }
        else if (c.tag == "SEND")
        {
            std::vector<std::uint32_t> words (c.body.size() / 4);
            if (! words.empty())
                std::memcpy (words.data(), c.body.data(), words.size() * 4);
            ch.sends.push_back (std::move (words));
        }
        else if (c.tag == "TRKH")
            ch.lanes.push_back (readLane (c.body));
    }
    return ch;
}

void readHead (Bytes body, Project& p)
{
    Reader r { body };
    r.u32();
    r.u32();
    p.title = r.fixed (256);
    p.tempo = r.f64 (120.0);
    r.f64();
    r.f64();
    r.f64();                 // (a legacy field the app skips)
    r.pos += 4 * 8 + 16;     // view state and the project GUID
    if (! r.has (8))
        return;
    p.root = r.u32(); // (round 190ds: +344 is the root 0..11, +348 the scale index; every file on disk agrees)
    p.scale = r.u32();
    r.u8();
    r.u8();
    r.u32(); // the channel-id counter
    if (r.has (4))
        p.appVersion = r.lp32();
}

void readMeta (Bytes body, Project& p)
{
    Reader r { body };
    p.created = r.fixed (256);
    r.f64();
    p.author = r.fixed (256);
    p.originalName = r.fixed (256);
    r.u8();
    r.pos += 2048; // notes
    p.saved = r.fixed (256);
}

struct Registered
{
    std::uint32_t id;
    const char* name;
    ModuleCategory category;
};

// FLM_FORMAT §7 (all 47 registrations)
constexpr Registered registry[] = {
    { 0, "MiniSynth", ModuleCategory::generator },
    { 1, "DW Sampler", ModuleCategory::generator },
    { 2, "GMSynth", ModuleCategory::generator },
    { 3, "Equalizer", ModuleCategory::eqFilter },
    { 4, "MultiFX", ModuleCategory::modulation },
    { 5, "Limiter (legacy)", ModuleCategory::dynamics },
    { 6, "Distortion", ModuleCategory::dynamics },
    { 7, "Graphic EQ", ModuleCategory::eqFilter },
    { 8, "Compressor", ModuleCategory::dynamics },
    { 9, "Filter", ModuleCategory::eqFilter },
    { 10, "Autoduck (legacy)", ModuleCategory::dynamics },
    { 11, "Chorus", ModuleCategory::modulation },
    { 12, "Reverb (legacy)", ModuleCategory::delayReverb },
    { 13, "Flanger", ModuleCategory::modulation },
    { 14, "Phaser", ModuleCategory::modulation },
    { 15, "Stereoizer", ModuleCategory::eqFilter },
    { 16, "Trance Delay", ModuleCategory::delayReverb },
    { 18, "Tape Delay", ModuleCategory::delayReverb },
    { 19, "Transistor Bass", ModuleCategory::generator },
    { 20, "Leveller", ModuleCategory::dynamics },
    { 23, "SuperSaw", ModuleCategory::generator },
    { 24, "Tuner", ModuleCategory::modulation },
    { 25, "Pitcher", ModuleCategory::modulation },
    { 26, "Analyzer", ModuleCategory::modulation },
    { 27, "Gate", ModuleCategory::dynamics },
    { 28, "Limiter", ModuleCategory::dynamics },
    { 29, "Autoduck", ModuleCategory::dynamics },
    { 30, "Tuned EQ", ModuleCategory::eqFilter },
    { 32, "Reverb", ModuleCategory::delayReverb },
    { 33, "Slicer", ModuleCategory::generator },
    { 34, "Spacer", ModuleCategory::eqFilter },
    { 35, "Reverb 2", ModuleCategory::delayReverb },
    { 36, "Multiband Compressor", ModuleCategory::dynamics },
    { 39, "3x Osc", ModuleCategory::generator },
    { 40, "Comb Filter", ModuleCategory::eqFilter },
    { 41, "Waveshaper", ModuleCategory::dynamics },
    { 42, "Tape Stop", ModuleCategory::modulation },
    { 43, "Wow & Flutter", ModuleCategory::modulation },
    { 44, "Spreader", ModuleCategory::eqFilter },
    { 45, "Pitcher 2", ModuleCategory::modulation },
    { 46, "Tremolo", ModuleCategory::modulation },
    { 47, "Note Echo", ModuleCategory::noteEffect },
    { 48, "Transpose", ModuleCategory::noteEffect },
    { 49, "Randomizer", ModuleCategory::noteEffect },
    { 50, "Scale", ModuleCategory::noteEffect },
    { 51, "Arpeggiator", ModuleCategory::noteEffect },
    { 52, "SoundFont Player", ModuleCategory::generator },
};

const Registered* registered (std::uint32_t type) noexcept
{
    for (const auto& r : registry)
        if (r.id == type)
            return &r;
    return nullptr;
}
} // namespace

const char* moduleName (std::uint32_t type) noexcept
{
    const auto* r = registered (type);
    return r != nullptr ? r->name : nullptr;
}

ModuleCategory moduleCategory (std::uint32_t type) noexcept
{
    const auto* r = registered (type);
    return r != nullptr ? r->category : ModuleCategory::unknown;
}

bool isFlmData (const std::uint8_t* data, std::size_t size) noexcept
{
    if (data == nullptr || size < 12 || std::memcmp (data, "10LF", 4) != 0)
        return false;
    const Bytes b (data, size);
    if (! isTag (b, 4))
        return false;
    std::uint32_t first;
    std::memcpy (&first, data + 8, 4);
    return (std::size_t) first <= size - 12;
}

std::optional<Project> parse (Bytes bytes, std::string* error)
{
    const auto fail = [error] (const char* why) -> std::optional<Project> {
        if (error != nullptr)
            *error = why;
        return std::nullopt;
    };
    if (! isFlmData (bytes.data(), bytes.size()))
        return fail ("not an FL Studio Mobile project");

    bool overrun = false;
    const auto top = chunksOf (bytes, 4, &overrun);
    if (overrun)
        return fail ("the project file is damaged (a chunk runs past its end)");

    Project p;
    for (const auto& c : top)
    {
        if (c.tag == "HEAD")
            readHead (c.body, p);
        else if (c.tag == "META")
            readMeta (c.body, p);
        else if (c.tag == "TDIV")
        {
            if (c.body.size() >= 2 && c.body[0] >= 1 && c.body[0] <= 32 && c.body[1] >= 1 && c.body[1] <= 32)
            {
                p.numerator = c.body[0];
                p.denominator = c.body[1];
            }
        }
        else if (c.tag == "KEYB")
        {
            Reader r { c.body };
            p.chordSetJson = r.lp32();
        }
        else if (c.tag == "RACK")
            p.racks.push_back (readRack (c.body));
        else if (c.tag == "CHNL")
            p.channels.push_back (readChannel (c.body));
        else
            ++p.unknownChunks;
    }
    if (p.channels.empty())
        return fail ("the project has no channels");
    return p;
}
} // namespace lynx::flm
