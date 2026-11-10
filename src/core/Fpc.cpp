#include "core/Fpc.h"

#include <algorithm>
#include <cstring>

namespace lynx::fpc
{
namespace
{
// Chunk ids (REA_rodada9 §1.1–1.4)
constexpr std::uint32_t idLoop = 1, idPad = 2, idOption3 = 3, idUiState = 4, idOption5 = 5;
constexpr std::uint32_t idPadParams = 0x64, idPadName = 0x65, idPadTrigger = 0x66, idEnvIndex = 0x67, idEnvBody = 0x68,
                        idLayer = 0x69, idPadEnd = 0x6A;
constexpr std::uint32_t idLayerPath = 0x12C, idLayerParams = 0x12D, idLayerEnd = 0x12E;

constexpr std::size_t envelopeHead = 12, envelopePoint = 24, envelopeTail = 36;

// Text is UTF-16 from version 2,000,014 on (FL's own kits: 2,000,014 and 2,000,100) and one byte per character
// before (FL Mobile's instrument kits: 1,000,011). A version of 0x200000 or more carries a build byte below the
// number (0x1E84E403 = 2,000,100, build 3). Chunk sizes are 64-bit in all of them.
bool wideText (std::uint32_t version) noexcept
{
    return (version >= 0x200000 ? version >> 8 : version) >= 2000014;
}

struct View
{
    std::uint32_t id = 0;
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
};

// The chunks of a container, in order. Stops at a chunk that runs past the end.
std::vector<View> chunksOf (const std::uint8_t* p, std::size_t n)
{
    std::vector<View> out;
    constexpr std::size_t head = 12;
    std::size_t pos = 0;

    while (pos + head <= n)
    {
        View c;
        std::uint64_t size = 0;
        std::memcpy (&c.id, p + pos, 4);

        std::memcpy (&size, p + pos + 4, 8);
        pos += head;

        if (size > n - pos)
            break;

        c.data = p + pos;
        c.size = (std::size_t) size;
        out.push_back (c);
        pos += c.size;
    }

    return out;
}

std::vector<std::uint8_t> bytesOf (const View& c)
{
    return c.size == 0 ? std::vector<std::uint8_t>() : std::vector<std::uint8_t> (c.data, c.data + c.size);
}

std::u16string textOf (const View& c, bool wide)
{
    if (! wide)
        return std::u16string (c.data, c.data + c.size); // each byte as its own character

    std::u16string s (c.size / 2, u'\0');

    for (std::size_t i = 0; i < s.size(); ++i)
        s[i] = (char16_t) ((std::uint32_t) c.data[i * 2] | ((std::uint32_t) c.data[i * 2 + 1] << 8));

    return s;
}

std::vector<std::int32_t> intsOf (const View& c)
{
    std::vector<std::int32_t> v (c.size / 4);
    if (! v.empty())
        std::memcpy (v.data(), c.data, v.size() * 4);
    return v;
}

RawChunk rawOf (const View& c, int position)
{
    return { position, c.id, bytesOf (c) };
}

void readEnvelopeBody (const View& c, Envelope& e)
{
    std::uint32_t count = 0;

    if (c.size >= envelopeHead + envelopeTail)
        std::memcpy (&count, c.data + 8, 4);

    if (c.size < envelopeHead + envelopeTail || (c.size - envelopeHead - envelopeTail) / envelopePoint != count
        || (c.size - envelopeHead - envelopeTail) % envelopePoint != 0)
    {
        e.unparsed = bytesOf (c);
        return;
    }

    std::memcpy (&e.headA, c.data, 4);
    std::memcpy (&e.headB, c.data + 4, 4);
    e.points.resize (count);

    for (std::size_t i = 0; i < count; ++i)
    {
        const auto* p = c.data + envelopeHead + i * envelopePoint;
        std::memcpy (&e.points[i].time, p, 8);
        std::memcpy (&e.points[i].value, p + 8, 8);
        std::memcpy (&e.points[i].tension, p + 16, 4);
        std::memcpy (&e.points[i].type, p + 20, 4);
    }

    std::memcpy (e.tail.data(), c.data + envelopeHead + count * envelopePoint, envelopeTail);
}

Layer readLayer (const View& chunk, bool wide)
{
    Layer layer;
    const auto list = chunksOf (chunk.data, chunk.size);
    bool hasPath = false, hasParams = false;

    for (std::size_t i = 0; i < list.size(); ++i)
    {
        const auto& c = list[i];

        if (c.id == idLayerPath && ! hasPath && (! wide || c.size % 2 == 0))
        {
            layer.path = textOf (c, wide);
            hasPath = true;
        }
        else if (c.id == idLayerParams && ! hasParams && c.size >= 28 && c.size % 4 == 0)
        {
            const auto v = intsOf (c);
            layer.volume = v[0];
            layer.pan = v[1];
            layer.velocityLow = v[2];
            layer.velocityHigh = v[3];
            layer.flags = v[4];
            layer.tune = v[5];
            layer.region = v[6];
            layer.moreParams.assign (v.begin() + 7, v.end());
            hasParams = true;
        }
        else if (c.id == idLayerEnd && c.size == 0 && i + 1 == list.size())
        {
        }
        else
            layer.extra.push_back (rawOf (c, (int) i));
    }

    return layer;
}

Pad readPad (const View& chunk, bool wide)
{
    Pad pad;
    const auto list = chunksOf (chunk.data, chunk.size);
    bool hasParams = false, hasName = false, hasTrigger = false, envelopeOpen = false;

    for (std::size_t i = 0; i < list.size(); ++i)
    {
        const auto& c = list[i];

        if (c.id == idPadParams && ! hasParams && c.size >= 64 && c.size % 4 == 0)
        {
            const auto v = intsOf (c);
            pad.volume = v[0];
            pad.pan = v[1];
            pad.flags = v[2];
            pad.note = v[3];
            pad.output = v[4];
            pad.cut = v[5];
            pad.cutBy = v[6];
            pad.tune = v[7];
            pad.colour = v[8];
            pad.icon = v[9];
            std::copy (v.begin() + 10, v.begin() + 16, pad.reserved.begin());
            pad.moreParams.assign (v.begin() + 16, v.end());
            hasParams = true;
        }
        else if (c.id == idPadName && ! hasName && (! wide || c.size % 2 == 0))
        {
            pad.name = textOf (c, wide);
            hasName = true;
        }
        else if (c.id == idPadTrigger && ! hasTrigger && c.size == 32)
        {
            std::memcpy (pad.trigger.data(), c.data, 32);
            hasTrigger = true;
        }
        else if (c.id == idLayer && pad.envelopes.empty())
            pad.layers.push_back (readLayer (c, wide));
        else if (c.id == idEnvIndex && c.size == 8 && ! envelopeOpen)
        {
            Envelope e;
            std::memcpy (&e.index, c.data, 4);
            std::memcpy (&e.indexReserved, c.data + 4, 4);
            pad.envelopes.push_back (std::move (e));
            envelopeOpen = true;
        }
        else if (c.id == idEnvBody && envelopeOpen)
        {
            readEnvelopeBody (c, pad.envelopes.back());
            envelopeOpen = false;
        }
        else if (c.id == idPadEnd && c.size == 0 && i + 1 == list.size())
        {
        }
        else
            pad.extra.push_back (rawOf (c, (int) i));
    }

    return pad;
}

// ---------------------------------------------------------------------------------------------------------------
// Writing

using Bytes = std::vector<std::uint8_t>;

struct Item
{
    std::uint32_t id = 0;
    Bytes body;
};

template <typename T>
void put (Bytes& out, const T& value)
{
    const auto* p = reinterpret_cast<const std::uint8_t*> (&value);
    out.insert (out.end(), p, p + sizeof (T));
}

Bytes textBytes (const std::u16string& s, bool wide)
{
    Bytes out;
    out.reserve (s.size() * 2);

    for (const auto ch : s)
    {
        if (wide)
        {
            out.push_back ((std::uint8_t) (ch & 0xFF));
            out.push_back ((std::uint8_t) (ch >> 8));
        }
        else
            out.push_back (ch < 0x100 ? (std::uint8_t) ch : (std::uint8_t) '?');
    }

    return out;
}

// The known chunks in FL's order, with the unknown ones put back at the index they were read at
Bytes container (std::vector<Item> items, const std::vector<RawChunk>& extra)
{
    for (const auto& x : extra)
        items.insert (items.begin() + std::clamp (x.position, 0, (int) items.size()), Item { x.id, x.body });

    Bytes out;

    for (const auto& item : items)
    {
        put (out, item.id);
        put (out, (std::uint64_t) item.body.size());

        out.insert (out.end(), item.body.begin(), item.body.end());
    }

    return out;
}

Bytes layerBytes (const Layer& layer, bool wide)
{
    Bytes params;
    for (const auto v : { layer.volume, layer.pan, layer.velocityLow, layer.velocityHigh, layer.flags, layer.tune, layer.region })
        put (params, v);
    for (const auto v : layer.moreParams)
        put (params, v);

    return container ({ { idLayerPath, textBytes (layer.path, wide) }, { idLayerParams, std::move (params) }, { idLayerEnd, {} } }, layer.extra);
}

Bytes envelopeBytes (const Envelope& e)
{
    if (! e.unparsed.empty())
        return e.unparsed;

    Bytes out;
    put (out, e.headA);
    put (out, e.headB);
    put (out, (std::uint32_t) e.points.size());

    for (const auto& p : e.points)
    {
        put (out, p.time);
        put (out, p.value);
        put (out, p.tension);
        put (out, p.type);
    }

    for (const auto v : e.tail)
        put (out, v);

    return out;
}

Bytes padBytes (const Pad& pad, bool wide)
{
    Bytes params;
    for (const auto v : { pad.volume, pad.pan, pad.flags, pad.note, pad.output, pad.cut, pad.cutBy, pad.tune, pad.colour, pad.icon })
        put (params, v);
    for (const auto v : pad.reserved)
        put (params, v);
    for (const auto v : pad.moreParams)
        put (params, v);

    Bytes trigger;
    for (const auto v : pad.trigger)
        put (trigger, v);

    std::vector<Item> items { { idPadParams, std::move (params) }, { idPadName, textBytes (pad.name, wide) }, { idPadTrigger, std::move (trigger) } };

    for (const auto& layer : pad.layers)
        items.push_back ({ idLayer, layerBytes (layer, wide) });

    for (const auto& e : pad.envelopes)
    {
        Bytes index;
        put (index, e.index);
        put (index, e.indexReserved);
        items.push_back ({ idEnvIndex, std::move (index) });
        items.push_back ({ idEnvBody, envelopeBytes (e) });
    }

    items.push_back ({ idPadEnd, {} });
    return container (std::move (items), pad.extra);
}
} // namespace

std::optional<Kit> read (const std::vector<std::uint8_t>& state)
{
    if (state.size() < 16)
        return std::nullopt;

    Kit kit;
    std::memcpy (&kit.version, state.data(), 4);

    if (kit.version < oldestVersion || kit.version > newestVersion)
        return std::nullopt;

    const bool wide = wideText (kit.version);
    const auto list = chunksOf (state.data() + 4, state.size() - 4);
    bool afterPads = false;

    for (std::size_t i = 0; i < list.size(); ++i)
    {
        const auto& c = list[i];

        if (c.id == idPad && ! afterPads)
        {
            kit.pads.push_back (readPad (c, wide));
            continue;
        }

        if (c.id == idLoop && ! kit.midiLoop && (! wide || c.size % 2 == 0) && ! kit.option3 && ! kit.option5 && ! kit.uiState)
            kit.midiLoop = textOf (c, wide);
        else if (c.id == idOption3 && ! kit.option3 && ! kit.option5 && ! kit.uiState)
            kit.option3 = bytesOf (c);
        else if (c.id == idOption5 && ! kit.option5 && ! kit.uiState)
            kit.option5 = bytesOf (c);
        else if (c.id == idUiState && ! kit.uiState)
            kit.uiState = bytesOf (c);
        else
            kit.extra.push_back (rawOf (c, (int) i));

        afterPads = true;
    }

    return kit;
}

std::vector<std::uint8_t> write (const Kit& kit)
{
    const bool wide = wideText (kit.version);
    std::vector<Item> items;

    for (const auto& pad : kit.pads)
        items.push_back ({ idPad, padBytes (pad, wide) });

    if (kit.midiLoop)
        items.push_back ({ idLoop, textBytes (*kit.midiLoop, wide) });
    if (kit.option3)
        items.push_back ({ idOption3, *kit.option3 });
    if (kit.option5)
        items.push_back ({ idOption5, *kit.option5 });
    if (kit.uiState)
        items.push_back ({ idUiState, *kit.uiState });

    Bytes out;
    put (out, kit.version);
    const auto body = container (std::move (items), kit.extra);
    out.insert (out.end(), body.begin(), body.end());
    return out;
}

Kit emptyKit()
{
    // FL's Empty.fst (FL Studio 2026)
    static constexpr int notes[32] = { 37, 36, 42, 54, 40, 38, 46, 44, 48, 47, 45, 43, 49, 55, 51, 53,
                                       60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75 };
    Envelope volume;
    volume.index = 0;
    volume.points = { { 0.0, 0.0, 0.0f, 0 }, { 0.75, 1.0, 0.2f, 0x1000000 }, { 1.5, 0.5, 0.2f, 0x1000000 }, { 0.25, 0.0, 0.2f, 0x1000000 } };
    volume.tail = { 0, 0, 1, -1, 2, 128, 128, 0, 128 };

    Envelope pan;
    pan.index = 1;
    pan.points = { { 0.0, 0.5, 0.2f, 0 } };

    Kit kit;

    for (const int note : notes)
    {
        Pad pad;
        pad.note = note;
        pad.layers.emplace_back();
        pad.envelopes = { volume, pan };
        kit.pads.push_back (std::move (pad));
    }

    kit.midiLoop = u"World Loops\\Polka\\fpc_polka_10.mid";
    kit.option3 = std::vector<std::uint8_t> { 1 };
    kit.uiState = std::vector<std::uint8_t> (32, 0);
    return kit;
}

std::string toUtf8 (const std::u16string& text)
{
    std::string out;

    for (std::size_t i = 0; i < text.size(); ++i)
    {
        std::uint32_t ch = text[i];

        if (ch >= 0xD800 && ch < 0xDC00 && i + 1 < text.size() && text[i + 1] >= 0xDC00 && text[i + 1] < 0xE000)
            ch = 0x10000 + ((ch - 0xD800) << 10) + ((std::uint32_t) text[++i] - 0xDC00);

        if (ch < 0x80)
            out += (char) ch;
        else if (ch < 0x800)
        {
            out += (char) (0xC0 | (ch >> 6));
            out += (char) (0x80 | (ch & 0x3F));
        }
        else if (ch < 0x10000)
        {
            out += (char) (0xE0 | (ch >> 12));
            out += (char) (0x80 | ((ch >> 6) & 0x3F));
            out += (char) (0x80 | (ch & 0x3F));
        }
        else
        {
            out += (char) (0xF0 | (ch >> 18));
            out += (char) (0x80 | ((ch >> 12) & 0x3F));
            out += (char) (0x80 | ((ch >> 6) & 0x3F));
            out += (char) (0x80 | (ch & 0x3F));
        }
    }

    return out;
}

std::u16string fromUtf8 (const std::string& text)
{
    std::u16string out;
    std::size_t i = 0;

    while (i < text.size())
    {
        const auto b = (std::uint8_t) text[i];
        const int extra = b < 0x80 ? 0 : (b >> 5) == 6 ? 1 : (b >> 4) == 14 ? 2 : (b >> 3) == 30 ? 3 : -1;
        std::uint32_t ch = extra == 0 ? b : extra == 1 ? (b & 0x1Fu) : extra == 2 ? (b & 0x0Fu) : (b & 0x07u);
        bool ok = extra >= 0 && i + (std::size_t) extra < text.size();

        for (int k = 1; ok && k <= extra; ++k)
        {
            const auto c = (std::uint8_t) text[i + (std::size_t) k];
            ok = (c >> 6) == 2;
            ch = (ch << 6) | (c & 0x3Fu);
        }

        if (! ok || ch > 0x10FFFF)
        {
            out += (char16_t) 0xFFFD;
            ++i;
            continue;
        }

        i += (std::size_t) extra + 1;

        if (ch < 0x10000)
            out += (char16_t) ch;
        else
        {
            ch -= 0x10000;
            out += (char16_t) (0xD800 + (ch >> 10));
            out += (char16_t) (0xDC00 + (ch & 0x3FF));
        }
    }

    return out;
}
} // namespace lynx::fpc
