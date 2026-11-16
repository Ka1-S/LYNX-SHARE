#include "core/Ncw.h"

#include <algorithm>
#include <cstring>

#include "core/SamplerImport.h"

namespace lynx::ncw
{
namespace
{
constexpr std::size_t kHeader = 120, kSubHeader = 16;
constexpr int kBlock = 512;

std::uint32_t u32 (const std::uint8_t* p) { std::uint32_t v; std::memcpy (&v, p, 4); return v; }
std::uint16_t u16 (const std::uint8_t* p) { return (std::uint16_t) (p[0] | p[1] << 8); }

bool readInfo (const std::uint8_t* d, std::size_t n, Info& info)
{
    if (! isNcw (d, n))
        return false;
    info.channels = u16 (d + 8);
    info.bits = u16 (d + 10);
    info.sampleRate = (double) u32 (d + 12);
    info.frames = u32 (d + 16);
    return info.channels >= 1 && info.channels <= 2 && (info.bits == 8 || info.bits == 16 || info.bits == 24 || info.bits == 32)
           && info.sampleRate >= 1000.0 && info.sampleRate <= 768000.0;
}

// 512 values of `width` bits, LSB first, sign-extended
bool unpack (const std::uint8_t* p, std::size_t avail, int width, std::int32_t* out)
{
    const auto need = (std::size_t) width * kBlock / 8;
    if (width < 1 || width > 32 || need > avail)
        return false;
    std::uint64_t acc = 0;
    int have = 0;
    std::size_t at = 0;
    for (int i = 0; i < kBlock; ++i)
    {
        while (have < width)
        {
            acc |= (std::uint64_t) p[at++] << have;
            have += 8;
        }
        auto v = (std::uint32_t) (acc & (width == 32 ? 0xFFFFFFFFull : ((1ull << width) - 1)));
        if (width < 32 && (v >> (width - 1)) & 1)
            v |= ~0u << width;
        out[i] = (std::int32_t) v;
        acc >>= width;
        have -= width;
    }
    return true;
}

// the samples as integers (PCM) or float bit patterns, per channel
bool decodeRaw (const std::uint8_t* d, std::size_t n, Info& info, std::vector<std::vector<std::int32_t>>& ch, std::string& error,
                std::int64_t maxFrames)
{
    if (! readInfo (d, n, info))
    {
        error = "not an NCW file";
        return false;
    }
    if (info.frames <= 0 || info.frames > maxFrames)
    {
        error = "unexpected length";
        return false;
    }
    const auto blocks = (std::size_t) ((info.frames + kBlock - 1) / kBlock);
    const std::size_t tableAt = u32 (d + 0x14), dataAt = u32 (d + 0x18), dataSize = u32 (d + 0x1C);
    if (tableAt < kHeader || tableAt > n || (n - tableAt) / 4 < blocks + 1 || dataAt > n || dataSize > n - dataAt)
    {
        error = "damaged block table";
        return false;
    }
    // (the frame count is bounded by the file: a block takes at least 16 + 128 bytes per channel, width 2)
    if (blocks > dataSize / ((kSubHeader + 128) * (std::size_t) info.channels) + 1)
    {
        error = "damaged block table";
        return false;
    }
    ch.assign ((std::size_t) info.channels, std::vector<std::int32_t> ((std::size_t) info.frames));
    std::int32_t values[kBlock];
    std::int32_t samples[2][kBlock];
    bool anyFloat = false;
    for (std::size_t b = 0; b < blocks; ++b)
    {
        const auto from = (std::size_t) u32 (d + tableAt + 4 * b), to = (std::size_t) u32 (d + tableAt + 4 * (b + 1));
        if (from >= to || to > dataSize)
        {
            error = "damaged block table";
            return false;
        }
        std::size_t at = dataAt + from;
        const std::size_t end = dataAt + to;
        bool midSide = false;
        for (int c = 0; c < info.channels; ++c)
        {
            if (at + kSubHeader > end || u32 (d + at) != 0x3E9A0C16u)
            {
                error = "damaged block";
                return false;
            }
            const auto base = (std::int32_t) u32 (d + at + 4);
            const auto width = (std::int16_t) u16 (d + at + 8);
            const auto flags = u16 (d + at + 10);
            at += kSubHeader;
            if (width == 0 || width == 1 || width < -32 || width > 32)
            {
                error = "unsupported block width";
                return false;
            }
            const int w = width < 0 ? -width : width;
            if (! unpack (d + at, end - at, w, values))
            {
                error = "damaged block";
                return false;
            }
            at += (std::size_t) w * kBlock / 8;
            auto* s = samples[c];
            if (width > 0)
            {
                auto v = (std::uint32_t) base;
                for (int i = 0; i < kBlock; ++i)
                {
                    s[i] = (std::int32_t) v;
                    v += (std::uint32_t) values[i]; // (wrapping; the 512th delta is never heard)
                }
            }
            else
                std::copy (values, values + kBlock, s);
            if (c == 0)
                midSide = (flags & 1) != 0;
            anyFloat = anyFloat || (flags & 2) != 0;
        }
        if (midSide && info.channels == 2)
        {
            const bool isFloat = info.bits == 32 && anyFloat;
            for (int i = 0; i < kBlock; ++i)
            {
                if (isFloat)
                {
                    float m, sd;
                    std::memcpy (&m, &samples[0][i], 4);
                    std::memcpy (&sd, &samples[1][i], 4);
                    const float l = m + sd, r = m - sd;
                    std::memcpy (&samples[0][i], &l, 4);
                    std::memcpy (&samples[1][i], &r, 4);
                }
                else
                {
                    const auto m = (std::uint32_t) samples[0][i], sd = (std::uint32_t) samples[1][i];
                    samples[0][i] = (std::int32_t) (m + sd);
                    samples[1][i] = (std::int32_t) (m - sd);
                }
            }
        }
        else if (midSide)
        {
            error = "mid / side in a mono file";
            return false;
        }
        const auto first = b * kBlock;
        const auto count = (std::size_t) std::min<std::int64_t> (kBlock, info.frames - (std::int64_t) first);
        for (int c = 0; c < info.channels; ++c)
            std::copy (samples[c], samples[c] + count, ch[(std::size_t) c].begin() + (std::ptrdiff_t) first);
    }
    info.isFloat = info.bits == 32 && anyFloat;
    return true;
}
} // namespace

bool isNcw (const std::uint8_t* d, std::size_t n)
{
    static const std::uint8_t sig[8] = { 0x01, 0xA8, 0x9E, 0xD6, 0x31, 0x01, 0x00, 0x00 };
    return n >= kHeader && std::memcmp (d, sig, 4) == 0 && (d[4] == 0x31 || d[4] == 0x30) && std::memcmp (d + 5, sig + 5, 3) == 0;
}

bool decode (const std::uint8_t* data, std::size_t size, Decoded& out, std::int64_t maxFrames)
{
    std::vector<std::vector<std::int32_t>> raw;
    out.channels.clear();
    if (! decodeRaw (data, size, out.info, raw, out.error, maxFrames))
        return false;
    const double scale = out.info.isFloat ? 1.0 : 1.0 / (double) (1u << (out.info.bits - 1));
    for (const auto& c : raw)
    {
        std::vector<float> v (c.size());
        for (std::size_t i = 0; i < c.size(); ++i)
        {
            if (out.info.isFloat)
                std::memcpy (&v[i], &c[i], 4);
            else
                v[i] = (float) ((double) c[i] * scale);
        }
        out.channels.push_back (std::move (v));
    }
    return true;
}

std::vector<std::uint8_t> toWav (const std::uint8_t* data, std::size_t size, std::string* error)
{
    Info info;
    std::vector<std::vector<std::int32_t>> raw;
    std::string e;
    if (! decodeRaw (data, size, info, raw, e, (std::int64_t) 1 << 28))
    {
        if (error != nullptr)
            *error = e;
        return {};
    }
    const int bytes = info.bits / 8;
    const auto frames = (std::size_t) info.frames;
    std::vector<std::uint8_t> pcm (frames * (std::size_t) info.channels * (std::size_t) bytes);
    auto* p = pcm.data();
    for (std::size_t i = 0; i < frames; ++i)
        for (int c = 0; c < info.channels; ++c)
        {
            auto v = (std::uint32_t) raw[(std::size_t) c][i];
            if (info.bits == 8)
                v += 0x80; // (WAV's 8-bit samples are unsigned)
            for (int k = 0; k < bytes; ++k)
                *p++ = (std::uint8_t) (v >> (8 * k));
        }
    return msimport::wavFile (pcm.data(), pcm.size(), info.channels, info.isFloat ? -32 : info.bits, info.sampleRate);
}
} // namespace lynx::ncw
