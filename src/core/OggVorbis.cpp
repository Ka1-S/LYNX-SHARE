#include "core/OggVorbis.h"

#include <algorithm>
#include <cstring>

#include <vorbis/codec.h>

namespace lynx::oggvorbis
{
bool isOggWav (const std::uint8_t* d, std::size_t size)
{
    if (size < 12 || std::memcmp (d, "RIFF", 4) != 0 || std::memcmp (d + 8, "WAVE", 4) != 0)
        return false;

    for (std::size_t pos = 12; pos + 8 <= size;)
    {
        std::uint32_t chunk = 0;
        std::memcpy (&chunk, d + pos + 4, 4);

        if (std::memcmp (d + pos, "fmt ", 4) == 0)
        {
            if (pos + 10 > size)
                return false;
            const auto tag = (std::uint16_t) (d[pos + 8] | (d[pos + 9] << 8));
            return tag == 0x674F || tag == 0x6750 || tag == 0x6751;
        }

        pos += 8 + (std::size_t) chunk + (chunk & 1);
    }

    return false;
}

bool isOggStream (const std::uint8_t* d, std::size_t size)
{
    return size >= 4 && std::memcmp (d, "OggS", 4) == 0;
}

namespace
{
// Hands the input to libogg in 4096-byte reads, as FL's loader reads its file.
struct PageReader
{
    const std::uint8_t* bytes;
    std::size_t size, pos = 0;
    ogg_sync_state sync {};

    PageReader (const std::uint8_t* b, std::size_t n) : bytes (b), size (n) { ogg_sync_init (&sync); }
    ~PageReader() { ogg_sync_clear (&sync); }
    PageReader (const PageReader&) = delete;
    PageReader& operator= (const PageReader&) = delete;

    bool next (ogg_page& page)
    {
        for (;;)
        {
            const auto r = ogg_sync_pageout (&sync, &page);

            if (r > 0)
                return true;

            if (r < 0)
                continue; // skipped bytes that are not a page

            if (pos >= size)
                return false;

            const auto n = std::min<std::size_t> (4096, size - pos);
            auto* buffer = ogg_sync_buffer (&sync, (long) n);

            if (buffer == nullptr)
                return false;

            std::memcpy (buffer, bytes + pos, n);
            pos += n;
            ogg_sync_wrote (&sync, (long) n);
        }
    }
};

struct Link
{
    ogg_stream_state stream {};
    vorbis_info info {};
    vorbis_comment comment {};
    vorbis_dsp_state dsp {};
    vorbis_block block {};
    bool synthesis = false;

    explicit Link (int serial)
    {
        ogg_stream_init (&stream, serial);
        vorbis_info_init (&info);
        vorbis_comment_init (&comment);
    }

    ~Link()
    {
        if (synthesis)
        {
            vorbis_block_clear (&block);
            vorbis_dsp_clear (&dsp);
        }
        ogg_stream_clear (&stream);
        vorbis_comment_clear (&comment);
        vorbis_info_clear (&info);
    }

    Link (const Link&) = delete;
    Link& operator= (const Link&) = delete;
};
} // namespace

namespace
{
// Round 190px (perf 7): the last Ogg page's granule position (the stream's length in samples per channel for a
// stream that starts at 0), or 0 when there is none to trust. Only a size hint for the decoded vectors: they used to
// grow by doubling, so a long stem ended with up to twice its samples' memory reserved (Thief's vocal: 77.7 MB of
// audio in 128 MB) and was copied at every doubling.
std::size_t lengthHint (const std::uint8_t* bytes, std::size_t size) noexcept
{
    for (std::size_t at = size >= 27 ? size - 27 : 0; at-- > 0;)
        if (bytes[at] == 'O' && bytes[at + 1] == 'g' && bytes[at + 2] == 'g' && bytes[at + 3] == 'S' && bytes[at + 4] == 0)
        {
            std::uint64_t g = 0;
            for (int k = 7; k >= 0; --k)
                g = (g << 8) | bytes[at + 6 + (std::size_t) k];
            // (−1: no packet ends on that page; anything beyond 24 hours at 384 kHz: not a length)
            return g == ~std::uint64_t { 0 } || g > 384000ull * 86400ull ? 0 : (std::size_t) g;
        }
    return 0;
}
} // namespace

bool decode (const std::uint8_t* bytes, std::size_t size, Decoded& out)
{
    out = {};

    if (bytes == nullptr || size == 0)
        return false;

    PageReader reader (bytes, size);
    ogg_page page {};
    ogg_packet packet {};

    // one pass per chained stream; the first page found starts each one
    while (reader.next (page))
    {
        Link link (ogg_page_serialno (&page));
        ogg_stream_pagein (&link.stream, &page);

        // the three Vorbis headers; anything else ends the decode
        int headers = 0;

        for (bool failed = false; headers < 3 && ! failed;)
        {
            const auto r = ogg_stream_packetout (&link.stream, &packet);

            if (r == 0)
            {
                if (headers == 0 || ! reader.next (page))
                    failed = true;
                else
                    ogg_stream_pagein (&link.stream, &page);
                continue;
            }

            if (r < 0 || vorbis_synthesis_headerin (&link.info, &link.comment, &packet) < 0)
                failed = true;
            else
                ++headers;
        }

        if (headers < 3)
            break;

        const auto channels = link.info.channels;

        if (channels < 1 || channels > 2 || (out.channels != 0 && channels != out.channels))
            break;

        if (out.channels == 0)
        {
            out.channels = channels;
            out.sampleRate = (int) link.info.rate;
            out.data.assign ((std::size_t) channels, {});
            if (const auto hint = lengthHint (bytes, size); hint > 0)
                for (auto& d : out.data)
                    d.reserve (hint + 8192); // (the last packets may run a little past the last granule)
        }

        if (vorbis_synthesis_init (&link.dsp, &link.info) != 0)
            break;

        vorbis_block_init (&link.dsp, &link.block);
        link.synthesis = true;

        // audio packets; the stream ends at the first end-of-stream page of any serial
        for (bool more = reader.next (page); more; more = reader.next (page))
        {
            ogg_stream_pagein (&link.stream, &page); // (refused for another serial)

            for (;;)
            {
                const auto r = ogg_stream_packetout (&link.stream, &packet);

                if (r == 0)
                    break;

                if (r < 0)
                    continue; // hole

                if (vorbis_synthesis (&link.block, &packet) == 0)
                    vorbis_synthesis_blockin (&link.dsp, &link.block);

                float** pcm = nullptr;

                for (int n; (n = vorbis_synthesis_pcmout (&link.dsp, &pcm)) > 0;)
                {
                    for (int c = 0; c < channels; ++c)
                        out.data[(std::size_t) c].insert (out.data[(std::size_t) c].end(), pcm[c], pcm[c] + n);
                    vorbis_synthesis_read (&link.dsp, n);
                }
            }

            if (ogg_page_eos (&page))
                break;
        }
    }

    // (round 190px: no large slack kept — a hint too small left the doubling's spare room, a chained file's more)
    for (auto& d : out.data)
        if (d.capacity() - d.size() > 16384)
            d.shrink_to_fit();
    return out.channels > 0;
}
} // namespace lynx::oggvorbis
