// LYNX Soundfont: reading .sf2 / .sf3 files (round 158). TinySoundFont merges the generators into regions; LYNX reads
// and validates the RIFF chunks itself (so damaged files are rejected before TinySoundFont indexes them) and keeps the
// samples as 16-bit. See core/SoundFont.h.

#include "core/Sfz.h"
#include "core/SoundFont.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>

#if defined(__clang__)
 #pragma clang diagnostic push
 #pragma clang diagnostic ignored "-Weverything"
#elif defined(__GNUC__)
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wall"
 #pragma GCC diagnostic ignored "-Wextra"
 #pragma GCC diagnostic ignored "-Wpedantic"
 #pragma GCC diagnostic ignored "-Wconversion"
 #pragma GCC diagnostic ignored "-Wsign-compare"
 #pragma GCC diagnostic ignored "-Wunused-function"
 #pragma GCC diagnostic ignored "-Wunused-parameter"
 #pragma GCC diagnostic ignored "-Wmisleading-indentation"
 #pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#elif defined(_MSC_VER)
 #pragma warning(push, 0)
#endif
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "../../third_party/stb_vorbis/stb_vorbis.c"
#define TSF_STATIC
#define TSF_NO_STDIO
#define TSF_IMPLEMENTATION
#include "../../third_party/tinysoundfont/tsf.h"
#if defined(__clang__)
 #pragma clang diagnostic pop
#elif defined(__GNUC__)
 #pragma GCC diagnostic pop
#elif defined(_MSC_VER)
 #pragma warning(pop)
#endif

namespace lynx::sf
{
namespace
{
std::uint32_t u32 (const std::uint8_t* p) noexcept { return (std::uint32_t) p[0] | ((std::uint32_t) p[1] << 8) | ((std::uint32_t) p[2] << 16) | ((std::uint32_t) p[3] << 24); }
std::uint16_t u16 (const std::uint8_t* p) noexcept { return (std::uint16_t) (p[0] | (p[1] << 8)); }

struct Span
{
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
};

// The chunks LYNX needs, located inside the file's bytes
struct Chunks
{
    Span inam, smpl, sm24;
    Span phdr, pbag, pmod, pgen, inst, ibag, imod, igen, shdr;
    int versionMajor = 2;
    bool ok = false;
    std::string error;
};

std::string clean20 (const std::uint8_t* p)
{
    std::string s;
    for (int i = 0; i < 20 && p[i] != 0; ++i)
        s.push_back ((char) (p[i] >= 32 && p[i] < 127 ? p[i] : '?'));
    while (! s.empty() && s.back() == ' ')
        s.pop_back();
    return s;
}

// Walks RIFF sfbk → LIST INFO / sdta / pdta. `data` may hold only the header part of a file (scan) when the sample
// chunk was skipped: then `smpl` points past the data that was read (size kept for the checks).
Chunks locate (const std::uint8_t* data, std::size_t size, bool needSamples)
{
    Chunks c;
    if (size < 12 || std::memcmp (data, "RIFF", 4) != 0 || std::memcmp (data + 8, "sfbk", 4) != 0)
    {
        c.error = "not a SoundFont (no RIFF sfbk header)";
        return c;
    }
    const auto riffEnd = std::min<std::size_t> (size, 8 + (std::size_t) u32 (data + 4));
    std::size_t pos = 12;
    while (pos + 8 <= riffEnd)
    {
        const auto* id = data + pos;
        const auto len = (std::size_t) u32 (data + pos + 4);
        const auto body = pos + 8;
        if (std::memcmp (id, "LIST", 4) == 0 && len >= 4 && body + 4 <= size)
        {
            const auto listEnd = std::min (body + len, size);
            const auto* type = data + body;
            std::size_t p = body + 4;
            while (p + 8 <= listEnd)
            {
                const auto* sid = data + p;
                const auto slen = (std::size_t) u32 (data + p + 4);
                const auto sbody = p + 8;
                const auto avail = sbody <= listEnd ? std::min (slen, listEnd - sbody) : 0;
                const Span span { data + sbody, avail };
                if (std::memcmp (type, "INFO", 4) == 0)
                {
                    if (std::memcmp (sid, "INAM", 4) == 0)
                        c.inam = span;
                    else if (std::memcmp (sid, "ifil", 4) == 0 && avail >= 2)
                        c.versionMajor = u16 (data + sbody);
                }
                else if (std::memcmp (type, "sdta", 4) == 0)
                {
                    if (std::memcmp (sid, "smpl", 4) == 0)
                        c.smpl = { data + sbody, needSamples ? avail : slen };
                    else if (std::memcmp (sid, "sm24", 4) == 0)
                        c.sm24 = span;
                }
                else if (std::memcmp (type, "pdta", 4) == 0)
                {
                    static const char* names[] = { "phdr", "pbag", "pmod", "pgen", "inst", "ibag", "imod", "igen", "shdr" };
                    Span* spans[] = { &c.phdr, &c.pbag, &c.pmod, &c.pgen, &c.inst, &c.ibag, &c.imod, &c.igen, &c.shdr };
                    for (int k = 0; k < 9; ++k)
                        if (std::memcmp (sid, names[k], 4) == 0)
                            *spans[k] = span;
                }
                if (slen > listEnd - std::min (listEnd, sbody))
                    break;
                p = sbody + slen + (slen & 1);
            }
        }
        if (len > riffEnd - std::min (riffEnd, body))
            break;
        pos = body + len + (len & 1);
    }

    const auto records = [] (const Span& s, std::size_t rec) { return s.data != nullptr && s.size % rec == 0 ? s.size / rec : 0; };
    if (records (c.phdr, 38) < 2 || records (c.pbag, 4) < 1 || records (c.pgen, 4) < 1 || records (c.inst, 22) < 2
        || records (c.ibag, 4) < 1 || records (c.igen, 4) < 1 || records (c.shdr, 46) < 2)
    {
        c.error = "incomplete SoundFont (preset data missing)";
        return c;
    }
    if (c.smpl.data == nullptr || c.smpl.size < 2)
    {
        c.error = "SoundFont without sample data";
        return c;
    }
    // the hydra's indices: every bag / generator range in bounds and in order
    const auto check = [] (const Span& owners, std::size_t rec, std::size_t at, std::size_t children) {
        const auto n = owners.size / rec;
        std::size_t last = 0;
        for (std::size_t i = 0; i < n; ++i)
        {
            const auto idx = (std::size_t) u16 (owners.data + i * rec + at);
            if (idx < last || idx > children)
                return false;
            last = idx;
        }
        return true;
    };
    if (! check (c.phdr, 38, 24, c.pbag.size / 4 - 1) || ! check (c.pbag, 4, 0, c.pgen.size / 4) || ! check (c.inst, 22, 20, c.ibag.size / 4 - 1)
        || ! check (c.ibag, 4, 0, c.igen.size / 4))
    {
        c.error = "damaged SoundFont (preset data out of range)";
        return c;
    }
    c.ok = true;
    return c;
}

FontInfo infoOf (const Chunks& c)
{
    FontInfo info;
    if (! c.ok)
        return info;
    if (c.inam.data != nullptr)
        for (std::size_t i = 0; i < c.inam.size && c.inam.data[i] != 0 && i < 256; ++i)
            info.name.push_back ((char) c.inam.data[i]);
    const auto n = c.phdr.size / 38;
    for (std::size_t i = 0; i + 1 < n; ++i)
    {
        const auto* r = c.phdr.data + i * 38;
        info.presets.push_back ({ clean20 (r), (int) u16 (r + 22), (int) u16 (r + 20) });
    }
    info.valid = true;
    return info;
}

template <typename T, std::size_t Rec>
T* readArray (const Span& s, int& count, void (*read) (T*, const std::uint8_t*))
{
    count = (int) (s.size / Rec);
    auto* out = (T*) std::calloc ((std::size_t) std::max (1, count), sizeof (T));
    if (out != nullptr)
        for (int i = 0; i < count; ++i)
            read (out + i, s.data + (std::size_t) i * Rec);
    return out;
}

void rdPhdr (tsf_hydra_phdr* o, const std::uint8_t* p)
{
    std::memcpy (o->presetName, p, 20);
    o->preset = u16 (p + 20), o->bank = u16 (p + 22), o->presetBagNdx = u16 (p + 24);
    o->library = u32 (p + 26), o->genre = u32 (p + 30), o->morphology = u32 (p + 34);
}
void rdBag (tsf_hydra_pbag* o, const std::uint8_t* p) { o->genNdx = u16 (p), o->modNdx = u16 (p + 2); }
void rdIbag (tsf_hydra_ibag* o, const std::uint8_t* p) { o->instGenNdx = u16 (p), o->instModNdx = u16 (p + 2); }
void rdPmod (tsf_hydra_pmod* o, const std::uint8_t* p)
{
    o->modSrcOper = u16 (p), o->modDestOper = u16 (p + 2), o->modAmount = (std::int16_t) u16 (p + 4), o->modAmtSrcOper = u16 (p + 6), o->modTransOper = u16 (p + 8);
}
void rdImod (tsf_hydra_imod* o, const std::uint8_t* p)
{
    o->modSrcOper = u16 (p), o->modDestOper = u16 (p + 2), o->modAmount = (std::int16_t) u16 (p + 4), o->modAmtSrcOper = u16 (p + 6), o->modTransOper = u16 (p + 8);
}
void rdPgen (tsf_hydra_pgen* o, const std::uint8_t* p) { o->genOper = u16 (p), o->genAmount.wordAmount = u16 (p + 2); }
void rdIgen (tsf_hydra_igen* o, const std::uint8_t* p) { o->genOper = u16 (p), o->genAmount.wordAmount = u16 (p + 2); }
void rdInst (tsf_hydra_inst* o, const std::uint8_t* p) { std::memcpy (o->instName, p, 20), o->instBagNdx = u16 (p + 20); }
void rdShdr (tsf_hydra_shdr* o, const std::uint8_t* p)
{
    std::memcpy (o->sampleName, p, 20);
    o->start = u32 (p + 20), o->end = u32 (p + 24), o->startLoop = u32 (p + 28), o->endLoop = u32 (p + 32), o->sampleRate = u32 (p + 36);
    o->originalPitch = p[40], o->pitchCorrection = (std::int8_t) p[41], o->sampleLink = u16 (p + 42), o->sampleType = u16 (p + 44);
}

struct Hydra
{
    tsf_hydra h {};
    ~Hydra()
    {
        std::free (h.phdrs), std::free (h.pbags), std::free (h.pmods), std::free (h.pgens), std::free (h.insts);
        std::free (h.ibags), std::free (h.imods), std::free (h.igens), std::free (h.shdrs);
    }
};

float timecentsOrSeconds (float v) noexcept { return std::isfinite (v) ? v : 0.0f; }

Envelope envelopeOf (const tsf_envelope& e)
{
    Envelope o;
    o.delay = timecentsOrSeconds (e.delay), o.attack = timecentsOrSeconds (e.attack), o.hold = timecentsOrSeconds (e.hold);
    o.decay = timecentsOrSeconds (e.decay), o.sustain = timecentsOrSeconds (e.sustain), o.release = timecentsOrSeconds (e.release);
    o.keynumToHold = e.keynumToHold, o.keynumToDecay = e.keynumToDecay;
    return o;
}
} // namespace

std::shared_ptr<const Font> parseFont (const std::vector<std::uint8_t>& bytes, std::string* error)
{
    const auto fail = [error] (const std::string& why) -> std::shared_ptr<const Font> {
        if (error != nullptr)
            *error = why;
        return nullptr;
    };
    const auto c = locate (bytes.data(), bytes.size(), true);
    if (! c.ok)
        return fail (c.error);

    Hydra hy;
    auto& h = hy.h;
    h.phdrs = readArray<tsf_hydra_phdr, 38> (c.phdr, h.phdrNum, rdPhdr);
    h.pbags = readArray<tsf_hydra_pbag, 4> (c.pbag, h.pbagNum, rdBag);
    h.pmods = readArray<tsf_hydra_pmod, 10> (c.pmod, h.pmodNum, rdPmod);
    h.pgens = readArray<tsf_hydra_pgen, 4> (c.pgen, h.pgenNum, rdPgen);
    h.insts = readArray<tsf_hydra_inst, 22> (c.inst, h.instNum, rdInst);
    h.ibags = readArray<tsf_hydra_ibag, 4> (c.ibag, h.ibagNum, rdIbag);
    h.imods = readArray<tsf_hydra_imod, 10> (c.imod, h.imodNum, rdImod);
    h.igens = readArray<tsf_hydra_igen, 4> (c.igen, h.igenNum, rdIgen);
    h.shdrs = readArray<tsf_hydra_shdr, 46> (c.shdr, h.shdrNum, rdShdr);
    if (h.phdrs == nullptr || h.pbags == nullptr || h.pmods == nullptr || h.pgens == nullptr || h.insts == nullptr || h.ibags == nullptr
        || h.imods == nullptr || h.igens == nullptr || h.shdrs == nullptr)
        return fail ("out of memory");

    // references TinySoundFont follows without checking: neutralise the ones out of range
    for (int i = 0; i < h.pgenNum; ++i)
        if (h.pgens[i].genOper == 41 && h.pgens[i].genAmount.wordAmount >= h.instNum - 1)
            h.pgens[i].genOper = 14; // (an unused generator)
    for (int i = 0; i < h.igenNum; ++i)
        if (h.igens[i].genOper == 53 && h.igens[i].genAmount.wordAmount >= h.shdrNum - 1)
            h.igens[i].genOper = 14;

    auto font = std::make_shared<Font>();
    if (c.inam.data != nullptr)
        for (std::size_t i = 0; i < c.inam.size && c.inam.data[i] != 0 && i < 256; ++i)
            font->name.push_back ((char) c.inam.data[i]);

    bool compressed = false;
    for (int i = 0; i + 1 < h.shdrNum; ++i)
        if ((h.shdrs[i].sampleType & 0x30) != 0)
            compressed = true;

    std::uint32_t frames = 0;
    if (! compressed)
    {
        frames = (std::uint32_t) (c.smpl.size / 2);
        font->samples.resize ((std::size_t) frames + 8, 0);
        for (std::uint32_t i = 0; i < frames; ++i)
            font->samples[i] = (std::int16_t) u16 (c.smpl.data + 2 * (std::size_t) i);
    }
    else
    {
        // SF3: every compressed sample's Ogg stream must lie inside the sample chunk
        for (int i = 0; i + 1 < h.shdrNum; ++i)
            if ((h.shdrs[i].sampleType & 0x30) != 0 && (h.shdrs[i].start > h.shdrs[i].end || h.shdrs[i].end > c.smpl.size))
                h.shdrs[i].start = h.shdrs[i].end = h.shdrs[i].startLoop = h.shdrs[i].endLoop = 0;
        float* decoded = nullptr;
        unsigned int count = (unsigned int) c.smpl.size;
        if (! tsf_decode_sf3_samples (c.smpl.data, &decoded, &count, &h))
        {
            std::free (decoded);
            return fail ("could not decode the SF3 samples");
        }
        frames = count;
        font->samples.resize ((std::size_t) frames + 8, 0);
        for (std::uint32_t i = 0; i < frames; ++i)
            font->samples[i] = (std::int16_t) std::lrint (std::clamp (decoded[i], -1.0f, 1.0f) * 32767.0f);
        std::free (decoded);
    }

    tsf parsed;
    std::memset (&parsed, 0, sizeof (parsed));
    if (! tsf_load_presets (&parsed, &h, frames))
        return fail ("out of memory");

    font->presets.reserve ((std::size_t) parsed.presetNum);
    for (int p = 0; p < parsed.presetNum; ++p)
    {
        const auto& src = parsed.presets[p];
        Preset preset;
        preset.name = clean20 ((const std::uint8_t*) src.presetName);
        preset.bank = src.bank, preset.program = src.preset;
        for (int r = 0; r < src.regionNum; ++r)
        {
            const auto& s = src.regions[r];
            Region o;
            o.loKey = s.lokey, o.hiKey = s.hikey, o.loVel = s.lovel, o.hiVel = s.hivel;
            if (o.loKey > o.hiKey || o.loVel > o.hiVel)
                continue;
            o.loop = s.loop_mode == TSF_LOOPMODE_CONTINUOUS ? LoopMode::continuous : s.loop_mode == TSF_LOOPMODE_SUSTAIN ? LoopMode::untilRelease : LoopMode::none;
            o.exclusiveClass = s.group;
            o.start = std::min (s.offset, frames);
            o.end = std::min (std::max (s.end, o.start), frames);
            if (o.end <= o.start + 1)
                continue;
            o.loopStart = s.loop_start;
            o.loopEnd = s.loop_end + 1; // (TinySoundFont keeps the last frame of the loop)
            if (o.loopStart < o.start || o.loopEnd > o.end || o.loopEnd < o.loopStart + 2)
                o.loop = LoopMode::none, o.loopStart = o.start, o.loopEnd = o.end;
            o.sampleRate = s.sample_rate >= 400 && s.sample_rate <= 400000 ? s.sample_rate : 44100;
            o.rootKey = std::clamp (s.pitch_keycenter, 0, 127);
            o.keyTrack = std::clamp (s.pitch_keytrack, 0, 1200);
            o.transpose = std::clamp (s.transpose, -120, 120), o.tune = std::clamp (s.tune, -9999, 9999);
            o.attenuation = std::clamp (s.attenuation * 100.0f, 0.0f, 1440.0f); // (TinySoundFont scales it by 0.01)
            o.pan = std::clamp (s.pan, -0.5f, 0.5f);
            o.ampEnv = envelopeOf (s.ampenv), o.modEnv = envelopeOf (s.modenv);
            o.filterFc = s.initialFilterFc, o.filterQ = s.initialFilterQ;
            o.modEnvToPitch = s.modEnvToPitch, o.modEnvToFilterFc = s.modEnvToFilterFc, o.modLfoToFilterFc = s.modLfoToFilterFc;
            o.modLfoToVolume = s.modLfoToVolume;
            o.delayModLfo = timecentsOrSeconds (s.delayModLFO), o.delayVibLfo = timecentsOrSeconds (s.delayVibLFO);
            o.freqModLfo = s.freqModLFO, o.freqVibLfo = s.freqVibLFO, o.modLfoToPitch = s.modLfoToPitch, o.vibLfoToPitch = s.vibLfoToPitch;
            o.chorusSend = std::clamp (s.chorusSend / 1000.0f, 0.0f, 1.0f), o.reverbSend = std::clamp (s.reverbSend / 1000.0f, 0.0f, 1.0f);
            preset.regions.push_back (o);
        }
        font->presets.push_back (std::move (preset));
    }
    for (int p = 0; p < parsed.presetNum; ++p)
        std::free (parsed.presets[p].regions);
    std::free (parsed.presets);

    if (font->presets.empty())
        return fail ("SoundFont without presets");
    return font;
}

std::shared_ptr<const Font> loadFontFile (const std::string& path, std::string* error)
{
    if (sfz::isSfzPath (path)) // an SFZ instrument (round 167o): its regions and samples as a one-preset font
        return sfz::loadFile (path, error);
    std::ifstream in (std::filesystem::path ((const char8_t*) path.c_str()), std::ios::binary);
    if (! in)
    {
        if (error != nullptr)
            *error = "file not found";
        return nullptr;
    }
    in.seekg (0, std::ios::end);
    const auto size = (std::size_t) std::max<std::streamoff> (0, in.tellg());
    in.seekg (0);
    std::vector<std::uint8_t> bytes;
    try
    {
        bytes.resize (size);
    }
    catch (const std::bad_alloc&)
    {
        if (error != nullptr)
            *error = "out of memory";
        return nullptr;
    }
    in.read ((char*) bytes.data(), (std::streamsize) size);
    if ((std::size_t) in.gcount() != size)
    {
        if (error != nullptr)
            *error = "could not read the file";
        return nullptr;
    }
    std::shared_ptr<const Font> font;
    try
    {
        font = parseFont (bytes, error);
    }
    catch (const std::bad_alloc&)
    {
        if (error != nullptr)
            *error = "out of memory";
        return nullptr;
    }
    if (font != nullptr)
    {
        auto f = std::const_pointer_cast<Font> (font);
        f->path = path;
        if (f->name.empty())
            f->name = std::filesystem::path ((const char8_t*) path.c_str()).stem().string();
    }
    return font;
}

FontInfo scanFont (const std::vector<std::uint8_t>& bytes)
{
    return infoOf (locate (bytes.data(), bytes.size(), true));
}

FontInfo scanFontFile (const std::string& path)
{
    if (sfz::isSfzPath (path))
        return sfz::scanFile (path);
    // reads the chunk headers and every chunk but the sample data
    std::ifstream in (std::filesystem::path ((const char8_t*) path.c_str()), std::ios::binary);
    if (! in)
        return {};
    in.seekg (0, std::ios::end);
    const auto fileSize = (std::uint64_t) std::max<std::streamoff> (0, in.tellg());
    in.seekg (0);
    std::uint8_t head[12];
    if (fileSize < 12 || ! in.read ((char*) head, 12) || std::memcmp (head, "RIFF", 4) != 0 || std::memcmp (head + 8, "sfbk", 4) != 0)
        return {};
    // a copy of the file with the sample chunk's contents left out (its header kept, its size intact)
    std::vector<std::uint8_t> bytes (head, head + 12);
    std::uint64_t pos = 12;
    while (pos + 12 <= fileSize && bytes.size() < (64u << 20))
    {
        std::uint8_t h[12];
        in.seekg ((std::streamoff) pos);
        if (! in.read ((char*) h, 12))
            break;
        const auto len = (std::uint64_t) u32 (h + 4);
        if (std::memcmp (h, "LIST", 4) == 0 && std::memcmp (h + 8, "sdta", 4) == 0)
        {
            // keep only the sub-chunk headers
            bytes.insert (bytes.end(), h, h + 12);
            const auto listAt = bytes.size() - 12;
            std::uint64_t p = pos + 12;
            const auto end = std::min (fileSize, pos + 8 + len);
            while (p + 8 <= end)
            {
                std::uint8_t s[8];
                in.seekg ((std::streamoff) p);
                if (! in.read ((char*) s, 8))
                    break;
                bytes.insert (bytes.end(), s, s + 8);
                const auto slen = (std::uint64_t) u32 (s + 4);
                p += 8 + slen + (slen & 1);
            }
            // the LIST's own size now covers only what was copied (the sample sizes stay in their headers)
            const auto copied = (std::uint32_t) (bytes.size() - listAt - 8);
            for (int k = 0; k < 4; ++k)
                bytes[listAt + 4 + (std::size_t) k] = (std::uint8_t) (copied >> (8 * k));
        }
        else
        {
            const auto take = std::min<std::uint64_t> (8 + len, fileSize - pos);
            if (take > (64u << 20))
                break;
            const auto at = bytes.size();
            bytes.resize (at + (std::size_t) take);
            in.seekg ((std::streamoff) pos);
            in.read ((char*) bytes.data() + at, (std::streamsize) take);
            if (len & 1)
                bytes.push_back (0);
        }
        pos += 8 + len + (len & 1);
    }
    // the RIFF size of the shortened copy
    const auto riff = (std::uint32_t) (bytes.size() - 8);
    for (int k = 0; k < 4; ++k)
        bytes[4 + (std::size_t) k] = (std::uint8_t) (riff >> (8 * k));
    // the sample chunk keeps its real size (its data was not read); locate() only checks it is there
    return infoOf (locate (bytes.data(), bytes.size(), false));
}

// --- a tiny generated SoundFont (tests, demo) -------------------------------------------------------------------------

namespace
{
struct Writer
{
    std::vector<std::uint8_t> b;
    void bytes (const void* p, std::size_t n) { b.insert (b.end(), (const std::uint8_t*) p, (const std::uint8_t*) p + n); }
    void w16 (std::uint32_t v) { b.push_back ((std::uint8_t) v), b.push_back ((std::uint8_t) (v >> 8)); }
    void w32 (std::uint32_t v) { w16 (v & 0xffff), w16 (v >> 16); }
    void id (const char* s) { bytes (s, 4); }
    void name20 (const std::string& s)
    {
        char n[20] {};
        std::memcpy (n, s.data(), std::min<std::size_t> (s.size(), 19));
        bytes (n, 20);
    }
    void chunk (const char* cid, const std::vector<std::uint8_t>& body)
    {
        id (cid), w32 ((std::uint32_t) body.size()), bytes (body.data(), body.size());
        if (body.size() & 1)
            b.push_back (0);
    }
};
} // namespace

std::vector<std::uint8_t> makeTestFont (const std::vector<TestPreset>& presets, int layers, int sampleRate)
{
    layers = std::clamp (layers, 1, 4);
    // samples: per preset, per key zone (low / high), per layer: 0.25 s of a sine whose amplitude encodes the layer,
    // with a loop over whole periods
    struct S { std::uint32_t start, end, loopStart, loopEnd; int root; };
    std::vector<std::int16_t> pcm;
    std::vector<S> shdrs;
    std::vector<std::string> sampleNames;
    const int len = sampleRate / 4;
    for (std::size_t p = 0; p < presets.size(); ++p)
        for (int zone = 0; zone < 2; ++zone)
            for (int layer = 0; layer < layers; ++layer)
            {
                const int root = zone == 0 ? 48 : 72;
                const double hz = 440.0 * std::pow (2.0, (root - 69) / 12.0);
                const double amp = (0.2 + 0.6 * (layer + 1) / layers) / (1.0 + (double) p); // louder layer = higher velocity; preset p at 1/(1+p)
                S s;
                s.start = (std::uint32_t) pcm.size();
                for (int i = 0; i < len; ++i)
                    pcm.push_back ((std::int16_t) std::lrint (32767.0 * amp * std::sin (2.0 * 3.14159265358979 * hz * i / sampleRate)));
                s.end = (std::uint32_t) pcm.size();
                const auto period = (double) sampleRate / hz;
                const auto periods = std::floor ((len * 0.5) / period);
                s.loopStart = s.start + (std::uint32_t) std::lrint (period * 4);
                s.loopEnd = s.loopStart + (std::uint32_t) std::lrint (period * periods);
                s.root = root;
                for (int i = 0; i < 46; ++i)
                    pcm.push_back (0); // SF2: 46 zero frames after each sample
                shdrs.push_back (s);
                sampleNames.push_back ("s" + std::to_string (shdrs.size()));
            }

    Writer pdta;
    pdta.id ("pdta");
    // instruments: one per preset; zones = 2 keys × layers velocity
    Writer phdr, pbag, pmod, pgen, inst, ibag, imod, igen, shdr;
    int pgenCount = 0, igenCount = 0, ibagCount = 0;
    const auto gen = [] (Writer& w, int op, int amount) { w.w16 ((std::uint32_t) op), w.w16 ((std::uint32_t) (amount & 0xffff)); };
    const auto range = [] (Writer& w, int op, int lo, int hi) { w.w16 ((std::uint32_t) op), w.b.push_back ((std::uint8_t) lo), w.b.push_back ((std::uint8_t) hi); };
    for (std::size_t p = 0; p < presets.size(); ++p)
    {
        phdr.name20 (presets[p].name), phdr.w16 ((std::uint32_t) presets[p].program), phdr.w16 ((std::uint32_t) presets[p].bank), phdr.w16 ((std::uint32_t) p);
        phdr.w32 (0), phdr.w32 (0), phdr.w32 (0);
        pbag.w16 ((std::uint32_t) pgenCount), pbag.w16 (0);
        gen (pgen, 41, (int) p), ++pgenCount;
        inst.name20 ("i" + std::to_string (p)), inst.w16 ((std::uint32_t) ibagCount);
        for (int zone = 0; zone < 2; ++zone)
            for (int layer = 0; layer < layers; ++layer)
            {
                ibag.w16 ((std::uint32_t) igenCount), ibag.w16 (0), ++ibagCount;
                range (igen, 43, zone == 0 ? 0 : 60, zone == 0 ? 59 : 127), ++igenCount;
                range (igen, 44, layer * 128 / layers, (layer + 1) * 128 / layers - 1), ++igenCount;
                gen (igen, 34, -7973), ++igenCount; // attack 10 ms
                gen (igen, 38, -3986), ++igenCount; // release 100 ms
                if (presets[p].exclusiveClass != 0)
                    gen (igen, 57, presets[p].exclusiveClass), ++igenCount;
                gen (igen, 54, presets[p].loop ? 1 : 0), ++igenCount;
                gen (igen, 53, (int) (p * 2 * (std::size_t) layers + (std::size_t) zone * (std::size_t) layers + (std::size_t) layer)), ++igenCount;
            }
    }
    phdr.name20 ("EOP"), phdr.w16 (0), phdr.w16 (0), phdr.w16 ((std::uint32_t) presets.size()), phdr.w32 (0), phdr.w32 (0), phdr.w32 (0);
    pbag.w16 ((std::uint32_t) pgenCount), pbag.w16 (0);
    pmod.b.assign (10, 0);
    gen (pgen, 0, 0);
    inst.name20 ("EOI"), inst.w16 ((std::uint32_t) ibagCount);
    ibag.w16 ((std::uint32_t) igenCount), ibag.w16 (0);
    imod.b.assign (10, 0);
    gen (igen, 0, 0);
    for (std::size_t i = 0; i < shdrs.size(); ++i)
    {
        const auto& s = shdrs[i];
        shdr.name20 (sampleNames[i]), shdr.w32 (s.start), shdr.w32 (s.end), shdr.w32 (s.loopStart), shdr.w32 (s.loopEnd);
        shdr.w32 ((std::uint32_t) sampleRate), shdr.b.push_back ((std::uint8_t) s.root), shdr.b.push_back (0), shdr.w16 (0), shdr.w16 (1);
    }
    shdr.name20 ("EOS"), shdr.w32 (0), shdr.w32 (0), shdr.w32 (0), shdr.w32 (0), shdr.w32 (0), shdr.b.push_back (0), shdr.b.push_back (0), shdr.w16 (0), shdr.w16 (0);
    pdta.chunk ("phdr", phdr.b), pdta.chunk ("pbag", pbag.b), pdta.chunk ("pmod", pmod.b), pdta.chunk ("pgen", pgen.b);
    pdta.chunk ("inst", inst.b), pdta.chunk ("ibag", ibag.b), pdta.chunk ("imod", imod.b), pdta.chunk ("igen", igen.b), pdta.chunk ("shdr", shdr.b);

    Writer info;
    info.id ("INFO");
    {
        Writer v;
        v.w16 (2), v.w16 (1);
        info.chunk ("ifil", v.b);
        const std::string n = "LYNX test font";
        std::vector<std::uint8_t> z (n.begin(), n.end());
        z.push_back (0);
        info.chunk ("INAM", z);
    }
    Writer sdta;
    sdta.id ("sdta");
    {
        std::vector<std::uint8_t> raw (pcm.size() * 2);
        for (std::size_t i = 0; i < pcm.size(); ++i)
            raw[2 * i] = (std::uint8_t) (pcm[i] & 0xff), raw[2 * i + 1] = (std::uint8_t) ((std::uint16_t) pcm[i] >> 8);
        sdta.chunk ("smpl", raw);
    }

    Writer out;
    out.id ("RIFF");
    out.w32 ((std::uint32_t) (4 + 8 + info.b.size() + 8 + sdta.b.size() + 8 + pdta.b.size()));
    out.id ("sfbk");
    out.chunk ("LIST", info.b);
    out.chunk ("LIST", sdta.b);
    out.chunk ("LIST", pdta.b);
    return out.b;
}
} // namespace lynx::sf
