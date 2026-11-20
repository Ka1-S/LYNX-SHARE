#include "core/DirectWaveState.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

namespace lynx::dw
{
namespace
{
template <typename T>
T get (const std::vector<std::uint8_t>& b, std::size_t at, T fallback = T {})
{
    if (at + sizeof (T) > b.size())
        return fallback;
    T v;
    std::memcpy (&v, b.data() + at, sizeof (T));
    return v;
}

template <typename T>
void put (std::vector<std::uint8_t>& b, std::size_t at, T v)
{
    if (b.size() < at + sizeof (T))
        b.resize (at + sizeof (T), 0);
    std::memcpy (b.data() + at, &v, sizeof (T));
}

void putU8 (std::vector<std::uint8_t>& b, std::size_t at, int v) { put<std::uint8_t> (b, at, (std::uint8_t) v); }

bool readList (const std::uint8_t* p, std::size_t n, std::vector<Node>& out, int depth)
{
    std::size_t o = 0;
    while (o < n)
    {
        if (n - o < 12)
            return false;
        Node node;
        std::uint64_t size = 0;
        std::memcpy (&node.id, p + o, 4);
        std::memcpy (&size, p + o + 4, 8);
        if (size > n - o - 12)
            return false;
        const auto* body = p + o + 12;
        node.container = (depth == 0 && node.id == 1) || (depth == 1 && node.id == 3);
        if (node.container)
        {
            if (! readList (body, (std::size_t) size, node.kids, depth + 1))
                return false;
        }
        else
            node.body.assign (body, body + size);
        out.push_back (std::move (node));
        o += 12 + (std::size_t) size;
    }
    return true;
}

void writeNode (const Node& node, std::vector<std::uint8_t>& out)
{
    const auto at = out.size();
    out.resize (at + 12);
    std::memcpy (out.data() + at, &node.id, 4);
    if (node.container)
        for (const auto& k : node.kids)
            writeNode (k, out);
    else
        out.insert (out.end(), node.body.begin(), node.body.end());
    const std::uint64_t size = out.size() - at - 12;
    std::memcpy (out.data() + at + 4, &size, 8);
}

std::vector<std::uint8_t> bodyOf (const Node& container)
{
    std::vector<std::uint8_t> out;
    for (const auto& k : container.kids)
        writeNode (k, out);
    return out;
}

Node leaf (std::uint32_t id, std::vector<std::uint8_t> body)
{
    Node n;
    n.id = id;
    n.body = std::move (body);
    return n;
}

Node leafText (std::uint32_t id, const std::string& s) { return leaf (id, std::vector<std::uint8_t> (s.begin(), s.end())); }

std::vector<std::uint8_t> zeros (std::size_t n) { return std::vector<std::uint8_t> (n, 0); }

std::vector<Node*> programNodes (Doc& doc)
{
    std::vector<Node*> v;
    for (auto& n : doc.top)
        if (n.container && n.id == 1)
            v.push_back (&n);
    return v;
}

std::vector<const Node*> programNodes (const Doc& doc)
{
    std::vector<const Node*> v;
    for (const auto& n : doc.top)
        if (n.container && n.id == 1)
            v.push_back (&n);
    return v;
}

// the k-th chunk `id` among a container's kids (null: none)
Node* kid (Node& c, std::uint32_t id, int k = 0)
{
    for (auto& n : c.kids)
        if (n.id == id && k-- == 0)
            return &n;
    return nullptr;
}

void setText (Node& c, std::uint32_t id, const std::string& s)
{
    if (auto* n = kid (c, id))
        n->body.assign (s.begin(), s.end());
}

// ------------------------------------------------------------------------------------------- program fields
void writeLfo (std::vector<std::uint8_t>& b, const Lfo& l)
{
    putU8 (b, 0, l.type); putU8 (b, 1, l.sync);
    put (b, 4, l.rate); put (b, 8, l.phase); put (b, 12, l.fade); put (b, 16, l.extra);
}

bool sameLfo (const Lfo& a, const Lfo& b)
{
    return a.type == b.type && a.sync == b.sync && a.rate == b.rate && a.phase == b.phase && a.fade == b.fade && a.extra == b.extra;
}

void patchProgram (Node& c, const Program& was, const Program& p)
{
    if (auto* n = kid (c, 0x64))
    {
        if (p.voiceMode != was.voiceMode) putU8 (n->body, 4, p.voiceMode);
        if (p.glideLegatoOnly != was.glideLegatoOnly) putU8 (n->body, 5, p.glideLegatoOnly ? 1 : 0);
        if (p.gain != was.gain) put (n->body, 6, p.gain);
        if (p.glide != was.glide) put (n->body, 10, p.glide);
        if (get<std::uint32_t> (n->body, 14) != (std::uint32_t) p.zones.size()) put (n->body, 14, (std::uint32_t) p.zones.size());
    }
    if (p.name != was.name) setText (c, 0x66, p.name);
    if (p.file != was.file) setText (c, 0x67, p.file);
    if (auto* n = kid (c, 0x68))
    {
        if (p.shape1 != was.shape1) putU8 (n->body, 0, p.shape1 ? 1 : 0);
        if (p.shape2 != was.shape2) putU8 (n->body, 1, p.shape2 ? 1 : 0);
        if (p.shape1Amount != was.shape1Amount) put (n->body, 2, p.shape1Amount);
        if (p.shape2Amount != was.shape2Amount) put (n->body, 6, p.shape2Amount);
    }
    if (auto* n = kid (c, 0x69))
    {
        if (p.delayOn != was.delayOn) putU8 (n->body, 0, p.delayOn ? 1 : 0);
        if (p.delayCross != was.delayCross) putU8 (n->body, 1, p.delayCross ? 1 : 0);
        for (int i = 0; i < 4; ++i)
            if (p.delay[i] != was.delay[i]) put (n->body, (std::size_t) (2 + 4 * i), p.delay[i]);
    }
    if (auto* n = kid (c, 0x6A))
    {
        if (p.reverbOn != was.reverbOn) putU8 (n->body, 0, p.reverbOn ? 1 : 0);
        for (int i = 0; i < 4; ++i)
            if (p.reverb[i] != was.reverb[i]) put (n->body, (std::size_t) (1 + 4 * i), p.reverb[i]);
    }
    if (auto* n = kid (c, 0x6B))
    {
        if (p.chorusOn != was.chorusOn) putU8 (n->body, 0, p.chorusOn ? 1 : 0);
        for (int i = 0; i < 4; ++i)
            if (p.chorus[i] != was.chorus[i]) put (n->body, (std::size_t) (1 + 4 * i), p.chorus[i]);
    }
    for (int k = 0; k < 2; ++k)
        if (! sameLfo (p.lfo[k], was.lfo[k]))
            if (auto* n = kid (c, 0x6C, k))
                writeLfo (n->body, p.lfo[k]);
    for (int k = 0; k < 4; ++k)
        if (p.modValue[k] != was.modValue[k])
            if (auto* n = kid (c, 0x6D, k))
                put (n->body, 0, p.modValue[k]);
    for (auto& n : c.kids)
        if (n.id == 0x6E && n.body.size() >= 13)
        {
            const auto i = get<std::int32_t> (n.body, 0);
            if (i < 1 || i > 99)
                continue;
            const auto& g = p.groups[(std::size_t) i];
            const auto& w = was.groups[(std::size_t) i];
            if (g.mode != w.mode) putU8 (n.body, 4, g.mode);
            if (g.probability != w.probability) put (n.body, 5, g.probability);
            if (g.velTolerance != w.velTolerance) put (n.body, 9, g.velTolerance);
        }
}

// ------------------------------------------------------------------------------------------- zone fields
void patchZone (Node& c, const Zone& was, const Zone& z)
{
    if (auto* n = kid (c, 0x1F4))
    {
        auto& b = n->body;
        if (z.root != was.root) putU8 (b, 0, z.root);
        if (z.lowKey != was.lowKey) putU8 (b, 1, z.lowKey);
        if (z.highKey != was.highKey) putU8 (b, 2, z.highKey);
        if (z.lowVel != was.lowVel) putU8 (b, 3, z.lowVel);
        if (z.highVel != was.highVel) putU8 (b, 4, z.highVel);
        if (z.exclusiveGroup != was.exclusiveGroup) putU8 (b, 5, z.exclusiveGroup);
        if (z.output != was.output) putU8 (b, 6, z.output);
        if (z.parallelFilters != was.parallelFilters) putU8 (b, 8, z.parallelFilters ? 1 : 0);
        if (z.gain != was.gain) put (b, 9, z.gain);
        if (z.pan != was.pan) put (b, 13, z.pan);
        if (z.altGroup != was.altGroup) put (b, 17, (std::int32_t) z.altGroup);
    }
    if (z.name != was.name) setText (c, 0x1F5, z.name);
    if (z.path != was.path) setText (c, 0x1F6, z.path);
    if (auto* n = kid (c, 0x1F7))
    {
        auto& b = n->body;
        if (z.frames != was.frames) put (b, 0, z.frames);
        if (z.channels != was.channels) put (b, 8, (std::int32_t) z.channels);
        if (z.beats != was.beats) put (b, 12, (std::int32_t) z.beats);
        if (z.rate != was.rate) put (b, 16, z.rate);
        if (z.loopMode != was.loopMode) put (b, 20, (std::int32_t) z.loopMode);
        if (z.loopA != was.loopA) put (b, 24, (std::int32_t) z.loopA);
        if (z.loopB != was.loopB) put (b, 28, (std::int32_t) z.loopB);
        if (z.offset != was.offset) put (b, 32, (std::int32_t) z.offset);
        if (z.bits != was.bits) put (b, 36, (std::int32_t) z.bits);
    }
    if (auto* n = kid (c, 0x1F8))
    {
        if (z.pitch != was.pitch) put (n->body, 0, z.pitch);
        if (z.coarse != was.coarse) put (n->body, 4, (std::int8_t) z.coarse);
        if (z.fine != was.fine) put (n->body, 5, (std::int8_t) z.fine);
        if (z.keyTrack != was.keyTrack) put (n->body, 6, (std::int16_t) z.keyTrack);
    }
    if (auto* n = kid (c, 0x1F9))
    {
        if (z.stretchMode != was.stretchMode) putU8 (n->body, 0, z.stretchMode);
        if (z.stretchSync != was.stretchSync) putU8 (n->body, 1, z.stretchSync);
        for (int i = 0; i < 3; ++i)
            if (z.stretch[i] != was.stretch[i]) put (n->body, (std::size_t) (2 + 4 * i), z.stretch[i]);
    }
    if (auto* n = kid (c, 0x1FA))
        for (int i = 0; i < 4; ++i)
            if (z.sends[i] != was.sends[i]) put (n->body, (std::size_t) (4 * i), z.sends[i]);
    for (int k = 0; k < 2; ++k)
        if (auto* n = kid (c, 0x1FB, k))
        {
            const auto& f = z.filter[k];
            const auto& w = was.filter[k];
            if (f.type != w.type) putU8 (n->body, 0, f.type);
            if (f.cutoff != w.cutoff) put (n->body, 4, f.cutoff);
            if (f.res != w.res) put (n->body, 8, f.res);
            if (f.shape != w.shape) put (n->body, 12, f.shape);
            if (f.extra != w.extra) put (n->body, 16, f.extra);
        }
    if (auto* n = kid (c, 0x1FC))
    {
        if (z.syncTempo != was.syncTempo) putU8 (n->body, 0, z.syncTempo ? 1 : 0);
        if (z.sliceKeys != was.sliceKeys) putU8 (n->body, 1, z.sliceKeys ? 1 : 0);
    }
    if (auto* n = kid (c, 0x1FD))
        for (int i = 0; i < 4; ++i)
            if (z.amp[i] != was.amp[i]) put (n->body, (std::size_t) (4 * i), z.amp[i]);
    for (int k = 0; k < 4; ++k)
        if (auto* n = kid (c, (std::uint32_t) (0x1FE + k)))
        {
            if (z.fxOn[k] != was.fxOn[k]) putU8 (n->body, 0, z.fxOn[k]);
            if (z.fx[k][0] != was.fx[k][0]) put (n->body, 1, z.fx[k][0]);
            if (z.fx[k][1] != was.fx[k][1]) put (n->body, 5, z.fx[k][1]);
        }
    for (int k = 0; k < 2; ++k)
        if (auto* n = kid (c, 0x202, k))
            for (int i = 0; i < 4; ++i)
                if (z.modEnv[k][i] != was.modEnv[k][i]) put (n->body, (std::size_t) (4 * i), z.modEnv[k][i]);
    for (int k = 0; k < 2; ++k)
        if (! sameLfo (z.lfo[k], was.lfo[k]))
            if (auto* n = kid (c, 0x203, k))
                writeLfo (n->body, z.lfo[k]);
    for (int k = 0; k < 16; ++k)
        if (auto* n = kid (c, 0x204, k))
        {
            const auto& m = z.mods[(std::size_t) k];
            const auto& w = was.mods[(std::size_t) k];
            if (m.src != w.src) put (n->body, 0, m.src);
            if (m.dst != w.dst) put (n->body, 2, m.dst);
            if (m.amount != w.amount) put (n->body, 4, m.amount);
        }
    // a new sample: the audio the zone embedded belonged to the old one
    if (z.path != was.path || z.frames != was.frames || z.embedded.empty())
        c.kids.erase (std::remove_if (c.kids.begin(), c.kids.end(), [] (const Node& n) { return n.id == 0x205 || n.id == 0x206; }), c.kids.end());
}

// A zone record with every field written (DirectWave_x64.dll's zone writer 0x1800410d0, in its order)
Node zoneNode (const Zone& z)
{
    Node c;
    c.id = 3;
    c.container = true;
    auto h = zeros (25);
    putU8 (h, 0, z.root); putU8 (h, 1, z.lowKey); putU8 (h, 2, z.highKey); putU8 (h, 3, z.lowVel); putU8 (h, 4, z.highVel);
    putU8 (h, 5, z.exclusiveGroup); putU8 (h, 6, z.output); putU8 (h, 8, z.parallelFilters ? 1 : 0);
    put (h, 9, z.gain); put (h, 13, z.pan); put (h, 17, (std::int32_t) z.altGroup); put (h, 21, (std::int32_t) 512);
    c.kids.push_back (leaf (0x1F4, h));
    c.kids.push_back (leafText (0x1F5, z.name));
    c.kids.push_back (leafText (0x1F6, z.path));
    auto f = zeros (40);
    put (f, 0, z.frames); put (f, 8, (std::int32_t) z.channels); put (f, 12, (std::int32_t) z.beats); put (f, 16, z.rate);
    put (f, 20, (std::int32_t) z.loopMode); put (f, 24, (std::int32_t) z.loopA); put (f, 28, (std::int32_t) z.loopB);
    put (f, 32, (std::int32_t) z.offset); put (f, 36, (std::int32_t) z.bits);
    c.kids.push_back (leaf (0x1F7, f));
    auto t = zeros (8);
    put (t, 0, z.pitch); put (t, 4, (std::int8_t) z.coarse); put (t, 5, (std::int8_t) z.fine); put (t, 6, (std::int16_t) z.keyTrack);
    c.kids.push_back (leaf (0x1F8, t));
    auto s = zeros (14);
    putU8 (s, 0, z.stretchMode); putU8 (s, 1, z.stretchSync);
    for (int i = 0; i < 3; ++i) put (s, (std::size_t) (2 + 4 * i), z.stretch[i]);
    c.kids.push_back (leaf (0x1F9, s));
    auto snd = zeros (48);
    for (int i = 0; i < 4; ++i) put (snd, (std::size_t) (4 * i), z.sends[i]);
    c.kids.push_back (leaf (0x1FA, snd));
    for (int k = 0; k < 2; ++k)
    {
        auto b = zeros (20);
        putU8 (b, 0, z.filter[k].type);
        put (b, 4, z.filter[k].cutoff); put (b, 8, z.filter[k].res); put (b, 12, z.filter[k].shape); put (b, 16, z.filter[k].extra);
        c.kids.push_back (leaf (0x1FB, b));
    }
    c.kids.push_back (leaf (0x1FC, { (std::uint8_t) (z.syncTempo ? 1 : 0), (std::uint8_t) (z.sliceKeys ? 1 : 0) }));
    auto amp = zeros (16);
    for (int i = 0; i < 4; ++i) put (amp, (std::size_t) (4 * i), z.amp[i]);
    c.kids.push_back (leaf (0x1FD, amp));
    for (int k = 0; k < 4; ++k)
    {
        auto b = zeros (9);
        putU8 (b, 0, z.fxOn[k]); put (b, 1, z.fx[k][0]); put (b, 5, z.fx[k][1]);
        c.kids.push_back (leaf ((std::uint32_t) (0x1FE + k), b));
    }
    for (int k = 0; k < 2; ++k)
    {
        auto b = zeros (16);
        for (int i = 0; i < 4; ++i) put (b, (std::size_t) (4 * i), z.modEnv[k][i]);
        c.kids.push_back (leaf (0x202, b));
    }
    for (int k = 0; k < 2; ++k)
    {
        auto b = zeros (20);
        writeLfo (b, z.lfo[k]);
        c.kids.push_back (leaf (0x203, b));
    }
    for (const auto& m : z.mods)
    {
        auto b = zeros (8);
        put (b, 0, m.src); put (b, 2, m.dst); put (b, 4, m.amount);
        c.kids.push_back (leaf (0x204, b));
    }
    if (! z.embedded.empty())
        c.kids.push_back (leaf (0x205, z.embedded));
    c.kids.push_back (leaf (4, {}));
    return c;
}

// An empty program record (the plugin database's DirectWave preset: every one of its 128 programs)
Node emptyProgram (int index)
{
    Node c;
    c.id = 1;
    c.container = true;
    auto h = zeros (30);
    put (h, 0, (std::int32_t) index);
    put (h, 6, 1.0f);
    c.kids.push_back (leaf (0x64, h));
    c.kids.push_back (leafText (0x66, "---"));
    c.kids.push_back (leafText (0x67, ""));
    c.kids.push_back (leaf (0x68, zeros (10)));
    auto d = zeros (18);
    put (d, 2, 0.75f); put (d, 6, 0.75f); put (d, 10, 0.5f); put (d, 14, 0.75f);
    c.kids.push_back (leaf (0x69, d));
    auto r = zeros (17);
    put (r, 1, 0.25f); put (r, 5, 0.5f); put (r, 9, 0.75f); put (r, 13, 0.25f);
    c.kids.push_back (leaf (0x6A, r));
    auto ch = zeros (17);
    put (ch, 1, 0.25f); put (ch, 5, 0.5f); put (ch, 9, 0.25f);
    c.kids.push_back (leaf (0x6B, ch));
    for (int k = 0; k < 2; ++k)
    {
        auto l = zeros (20);
        put (l, 4, 0.1f); put (l, 8, 1.0f);
        c.kids.push_back (leaf (0x6C, l));
    }
    for (int k = 0; k < 4; ++k)
        c.kids.push_back (leaf (0x6D, zeros (4)));
    for (int g = 0; g < 100; ++g)
    {
        auto b = zeros (13);
        put (b, 0, (std::int32_t) g);
        put (b, 5, 1.0f);
        c.kids.push_back (leaf (0x6E, b));
    }
    c.kids.push_back (leaf (2, {}));
    return c;
}

Node* slotNode (Doc& doc, int slot)
{
    for (auto& n : doc.top)
        if (n.id == 5 && n.body.size() >= 521 && get<std::int32_t> (n.body, 0) == slot)
            return &n;
    return nullptr;
}
} // namespace

std::optional<Doc> readDoc (const std::vector<std::uint8_t>& state)
{
    if (state.size() < 4)
        return std::nullopt;
    Doc doc;
    std::memcpy (&doc.version, state.data(), 4);
    if (doc.version < 36 || doc.version > 38)
        return std::nullopt;
    if (! readList (state.data() + 4, state.size() - 4, doc.top, 0))
        return std::nullopt;
    return doc;
}

std::vector<std::uint8_t> writeDoc (const Doc& doc)
{
    std::vector<std::uint8_t> out (4);
    std::memcpy (out.data(), &doc.version, 4);
    for (const auto& n : doc.top)
        writeNode (n, out);
    return out;
}

int completeShortZoneRecords (Doc& doc)
{
    int completed = 0;
    for (auto& program : doc.top)
        if (program.id == 1 && program.container)
            for (auto& zone : program.kids)
                if (zone.id == 3 && zone.container)
                    for (auto& record : zone.kids)
                        if (const auto want = zoneRecordSize (record.id); ! record.container && record.body.size() < want)
                        {
                            const auto had = record.body.size();
                            record.body.resize (want, 0);
                            if (record.id == 0x1F7 && had <= 36)
                                put (record.body, 36, (std::int32_t) 32);
                            ++completed;
                        }
    return completed;
}

Doc freshDoc()
{
    Doc doc;
    doc.version = 37;
    doc.top.push_back (leaf (6, zeros (16)));
    doc.top.push_back (leaf (0x206, { 1, 0, 0, 0, 0 }));
    for (int k = 0; k < 16; ++k)
    {
        auto b = zeros (628);
        put (b, 0, (std::int32_t) k);
        putU8 (b, 4, 0x3c);
        putU8 (b, 520, k);
        putU8 (b, 532, 0xc8);
        put (b, 572, 100.0f / 127.0f);
        put (b, 576, 0.5f);
        put (b, 580, 1.0f);
        doc.top.push_back (leaf (5, b));
    }
    for (int p = 0; p < 128; ++p)
        doc.top.push_back (emptyProgram (p));
    return doc;
}

int programCount (const Doc& doc) { return (int) programNodes (doc).size(); }

Program program (const Doc& doc, int index)
{
    const auto nodes = programNodes (doc);
    if (index < 0 || index >= (int) nodes.size())
        return {};
    const auto body = bodyOf (*nodes[(std::size_t) index]);
    return parseProgram (body.data(), body.size());
}

void setProgram (Doc& doc, int index, const Program& edited)
{
    auto nodes = programNodes (doc);
    if (index < 0 || index >= (int) nodes.size())
        return;
    auto& c = *nodes[(std::size_t) index];
    const auto was = program (doc, index);
    // the zone records as they are now, by their place
    std::vector<Node> oldZones;
    std::vector<Node> rest;
    for (auto& n : c.kids)
        (n.container && n.id == 3 ? oldZones : rest).push_back (std::move (n));
    std::vector<Node> zones;
    for (const auto& z : edited.zones)
    {
        if (z.origin >= 0 && z.origin < (int) oldZones.size())
        {
            auto node = oldZones[(std::size_t) z.origin];
            patchZone (node, was.zones[(std::size_t) z.origin], z);
            zones.push_back (std::move (node));
        }
        else
            zones.push_back (zoneNode (z));
    }
    // the record's order: the program's own chunks, its zones, the end mark (2)
    c.kids.clear();
    Node end;
    bool hasEnd = false;
    for (auto& n : rest)
    {
        if (n.id == 2 && ! n.container)
        {
            end = std::move (n);
            hasEnd = true;
            continue;
        }
        c.kids.push_back (std::move (n));
    }
    for (auto& z : zones)
        c.kids.push_back (std::move (z));
    c.kids.push_back (hasEnd ? std::move (end) : leaf (2, {}));
    patchProgram (c, was, edited);
}

int slotProgram (const Doc& doc, int slot)
{
    for (const auto& n : doc.top)
        if (n.id == 5 && n.body.size() >= 521 && get<std::int32_t> (n.body, 0) == slot)
            return n.body[520];
    return slot == 0 ? 0 : -1;
}

void setSlotProgram (Doc& doc, int slot, int programIndex)
{
    if (auto* n = slotNode (doc, slot))
        putU8 (n->body, 520, std::clamp (programIndex, 0, 127));
}

Zone newZone (const std::string& path, const std::string& name, std::int64_t frames, int channels, float rate, int bits,
              int root, int lowKey, int highKey)
{
    // the zone reset 0x180035870, then the sample load 0x18001ddb0 (REA_rodada190hz_dw.md §2)
    Zone z;
    z.root = root; z.lowKey = lowKey; z.highKey = highKey; z.lowVel = 0; z.highVel = 127;
    z.exclusiveGroup = 0; z.output = 0; z.parallelFilters = false;
    z.gain = 1.0f; z.pan = 0.5f; z.altGroup = 0;
    z.frames = frames; z.channels = channels; z.rate = rate; z.bits = bits;
    z.loopMode = 0; z.loopA = 0; z.loopB = 0; z.offset = 0;
    // "ticks": the length in half seconds to the nearest power of two (at least 1), × 4
    int halves = rate > 0.0f ? (int) (frames * 2 / (std::int64_t) (int) rate) : 0;
    int beats = 1;
    if (halves >= 2)
    {
        int lo = 1;
        for (int p = 1; p < 31; ++p)
        {
            const int hi = 1 << p;
            if (halves == hi) { beats = hi; break; }
            if (lo < halves && halves < hi) { beats = halves - lo < hi - halves ? lo : hi; break; }
            lo = hi;
        }
    }
    z.beats = frames > 0 && rate > 0.0f ? beats * 4 : 16;
    z.pitch = 0.5f; z.coarse = 0; z.fine = 0; z.keyTrack = 100;
    z.stretchMode = 0; z.stretchSync = 0;
    z.stretch[0] = 0.5f; z.stretch[1] = 0.5f; z.stretch[2] = 1.0f;
    z.sends[0] = 0.5f; z.sends[1] = 0.5f; z.sends[2] = 0.5f; z.sends[3] = 1.0f;
    const auto cutoff = 5.0f / 10.583333f; // (log2 (1) + 5) / 10.583333: 440 Hz on the cutoff law
    for (auto& f : z.filter)
    {
        f.type = 0; f.cutoff = cutoff; f.res = 0.5f; f.shape = 0.0f; f.extra = 0.0f;
    }
    z.syncTempo = false; z.sliceKeys = false;
    z.amp[0] = 0.0f; z.amp[1] = 0.5f; z.amp[2] = 1.0f; z.amp[3] = 0.25f;
    for (int k = 0; k < 4; ++k)
        z.fxOn[k] = 0;
    z.fx[0][0] = 0.0f; z.fx[0][1] = 1.0f; // ring modulator rate, mix
    z.fx[1][0] = 0.0f; z.fx[1][1] = 1.0f; // decimator step, mix
    z.fx[2][0] = 0.0f; z.fx[2][1] = 1.0f; // bit reducer bits, mix
    z.fx[3][0] = 0.5f; z.fx[3][1] = 1.0f; // phaser frequency, mix
    for (auto& e : z.modEnv)
    {
        e[0] = 0.0f; e[1] = 0.5f; e[2] = 0.5f; e[3] = 0.25f;
    }
    z.lfo[0] = Lfo { 0, 0, 0.1f, 1.0f, 0.0f, 0.0f };
    z.lfo[1] = Lfo { 0, 0, 0.55f, 1.0f, 0.0f, 0.0f };
    for (auto& m : z.mods)
        m = ModSlot { 0, 0, 0.5f };
    z.mods[0] = ModSlot { 2, 2, 1.0f };   // Note Velocity → Voice Gain
    z.mods[1] = ModSlot { 3, 34, 0.75f }; // Mod Wheel → Mod Amt P1-3
    z.mods[2] = ModSlot { 12, 1, 0.5f };  // Zone Lfo 2 → Voice Pitch
    z.path = path;
    z.name = name;
    z.origin = -1;
    return z;
}

std::vector<std::uint8_t> writeProgramFile (const Doc& doc, int index)
{
    std::vector<std::uint8_t> out { 'D', 'w', 'P', 'r' };
    const std::uint32_t version = 38;
    out.insert (out.end(), (const std::uint8_t*) &version, (const std::uint8_t*) &version + 4);
    Node flags = leaf (6, zeros (16));
    for (const auto& n : doc.top)
        if (n.id == 6 && ! n.container)
            flags = n;
    writeNode (flags, out);
    const auto nodes = programNodes (doc);
    if (index >= 0 && index < (int) nodes.size())
        writeNode (*nodes[(std::size_t) index], out);
    return out;
}

std::optional<Program> readProgramFile (const std::vector<std::uint8_t>& file)
{
    if (file.size() < 8 || std::memcmp (file.data(), "DwPr", 4) != 0)
        return std::nullopt;
    const auto doc = readDoc (std::vector<std::uint8_t> (file.begin() + 4, file.end()));
    if (! doc || programCount (*doc) < 1)
        return std::nullopt;
    return program (*doc, 0);
}

void loadProgram (Doc& doc, int index, Program loaded)
{
    for (auto& z : loaded.zones)
        z.origin = -1;
    auto nodes = programNodes (doc);
    if (index < 0 || index >= (int) nodes.size())
        return;
    // the record starts as an empty program at its place, then takes every field of the loaded one
    *nodes[(std::size_t) index] = emptyProgram (index);
    setProgram (doc, index, loaded);
}

std::vector<std::uint8_t> writeBankFile (const Doc& doc)
{
    std::vector<std::uint8_t> out { 'D', 'w', 'B', 'k' };
    const auto state = writeDoc (doc);
    out.insert (out.end(), state.begin(), state.end());
    return out;
}

std::optional<Doc> readBankFile (const std::vector<std::uint8_t>& file)
{
    if (file.size() < 8 || std::memcmp (file.data(), "DwBk", 4) != 0)
        return std::nullopt;
    return readDoc (std::vector<std::uint8_t> (file.begin() + 4, file.end()));
}

void automap (std::vector<Zone>& zones, const std::vector<bool>& hasSample)
{
    // FL's "Automap zone(s)" (0x180013d50): a zone with a sample spans from its root up to the next higher root − 1
    // (127 when none); then the zones starting at the lowest start reach down to key 0
    const auto n = zones.size();
    for (std::size_t i = 0; i < n; ++i)
    {
        if (i < hasSample.size() && ! hasSample[i])
            continue;
        auto& z = zones[i];
        z.lowKey = z.root;
        z.highKey = 127;
        for (std::size_t j = 0; j < n; ++j)
            if (j != i && zones[j].root > z.root && zones[j].root <= z.highKey)
                z.highKey = zones[j].root - 1;
    }
    if (n == 0)
        return;
    int lowest = 127;
    for (const auto& z : zones)
        lowest = std::min (lowest, z.lowKey);
    for (auto& z : zones)
        if (z.lowKey == lowest)
            z.lowKey = 0;
}

void remapAsDrumkit (std::vector<Zone>& zones, const std::vector<bool>& hasSample)
{
    // (0x180018c17, command 16607): zone i's root = (60 + i) mod 128, then Automap
    for (std::size_t i = 0; i < zones.size(); ++i)
        zones[i].root = (int) ((60 + i) % 128);
    automap (zones, hasSample);
}

void copyToZones (std::vector<Zone>& zones, int from, int what, const std::vector<bool>& targets)
{
    if (from < 0 || from >= (int) zones.size())
        return;
    const auto& s = zones[(std::size_t) from];
    for (std::size_t i = 0; i < zones.size(); ++i)
    {
        if ((int) i == from || zones[i].output != 0 || (i < targets.size() && ! targets[i]))
            continue;
        auto& z = zones[i];
        switch (what)
        {
            case CopyKeyTracking: z.keyTrack = s.keyTrack; break;
            case CopyFineTune: z.fine = s.fine; break;
            case CopyTicks: z.beats = s.beats; break;
            case CopyTranspose: z.coarse = s.coarse; break;
            case CopyBeatSync: z.syncTempo = s.syncTempo; break;
            case CopyWaveSlice: z.sliceKeys = s.sliceKeys; break;
            case CopyMuteGroup: z.exclusiveGroup = s.exclusiveGroup; break;
            case CopySampleRate: z.rate = s.rate; break;
            case CopyRootKey: z.root = s.root; break;
            case CopyLowKey: z.lowKey = s.lowKey; break;
            case CopyHighKey: z.highKey = s.highKey; break;
            case CopyLowVelocity: z.lowVel = s.lowVel; break;
            case CopyHighVelocity: z.highVel = s.highVel; break;
            default: break;
        }
    }
}

std::optional<WavInfo> readWavInfo (const std::vector<std::uint8_t>& w)
{
    if (w.size() < 12 || std::memcmp (w.data(), "RIFF", 4) != 0 || std::memcmp (w.data() + 8, "WAVE", 4) != 0)
        return std::nullopt;
    WavInfo info;
    bool any = false;
    std::size_t o = 12;
    while (o + 8 <= w.size())
    {
        std::uint32_t sz = 0;
        std::memcpy (&sz, w.data() + o + 4, 4);
        const auto* b = w.data() + o + 8;
        const auto avail = std::min<std::size_t> (sz, w.size() - o - 8);
        if (std::memcmp (w.data() + o, "smpl", 4) == 0 && avail >= 36)
        {
            std::int32_t unity = 0, loops = 0;
            std::memcpy (&unity, b + 12, 4);
            std::memcpy (&loops, b + 28, 4);
            info.root = unity;
            any = true;
            if (loops > 0 && avail >= 36 + 24)
            {
                std::int32_t start = 0, end = 0;
                std::memcpy (&start, b + 36 + 8, 4);
                std::memcpy (&end, b + 36 + 12, 4);
                info.loopMode = 2;
                info.loopA = start;
                info.loopB = end + 1;
            }
        }
        else if (std::memcmp (w.data() + o, "inst", 4) == 0 && avail >= 7)
        {
            info.root = b[0];
            info.fine = (std::int8_t) b[1];
            info.lowKey = b[3];
            info.highKey = b[4];
            info.lowVel = b[5];
            info.highVel = b[6];
            info.hasInst = true;
            any = true;
        }
        o += 8 + (std::size_t) sz + (sz & 1);
    }
    if (! any)
        return WavInfo {};
    return info;
}

std::string zoneNameOf (const std::string& path)
{
    // 0x180003c70: the path cut at its last '.', then what follows the last '\\' or '/', at most 127 characters
    auto s = path;
    const auto dot = s.find_last_of ('.');
    if (dot != std::string::npos)
        s = s.substr (0, dot);
    const auto slash = s.find_last_of ("\\/");
    if (slash != std::string::npos)
        s = s.substr (slash + 1);
    return s.substr (0, 127);
}

Program programFromSfz (const sfz::Parsed& parsed, const std::string& folder, const std::function<std::string (const std::string&)>& resolve)
{
    Program p;
    auto toF = [] (const std::string& v, float fallback) {
        if (v.empty())
            return fallback;
        try { return std::stof (v); } catch (...) { return fallback; }
    };
    for (const auto& r : parsed.regions)
    {
        auto sample = r.get ("sample");
        if (sample.empty() || sample[0] == '*')
            continue;
        sample = r.get ("__default_path") + sample;
        std::replace (sample.begin(), sample.end(), '\\', '/');
        const auto joined = folder.empty() || sample[0] == '/' || (sample.size() > 1 && sample[1] == ':') ? sample : folder + "/" + sample;
        const auto path = resolve ? resolve (joined) : joined;
        if (path.empty())
            continue;
        const auto offsetNotes = (int) toF (r.get ("__note_offset"), 0.0f) + 12 * (int) toF (r.get ("__octave_offset"), 0.0f);
        auto note = [&] (const char* opcode, int fallback) {
            const auto v = r.get (opcode);
            const auto n = v.empty() ? -1 : sfz::noteNumber (v);
            return n < 0 ? fallback : n + offsetNotes;
        };
        int lo = note ("lokey", 0), hi = note ("hikey", 127), root = note ("pitch_keycenter", 60);
        if (const auto k = note ("key", -1); k >= 0)
            lo = hi = k, root = note ("pitch_keycenter", k);
        auto z = newZone (path, zoneNameOf (path), 0, 0, 44100.0f, 32, std::clamp (root, 0, 127), std::clamp (lo, 0, 127), std::clamp (hi, 0, 127));
        z.lowVel = std::clamp ((int) toF (r.get ("lovel"), 0.0f), 0, 127);
        z.highVel = std::clamp ((int) toF (r.get ("hivel"), 127.0f), 0, 127);
        if (z.lowKey > z.highKey || z.lowVel > z.highVel)
            continue;
        z.keyTrack = (int) toF (r.get ("pitch_keytrack"), 100.0f);
        z.coarse = std::clamp ((int) toF (r.get ("transpose"), 0.0f), -127, 127);
        // tune (cents) on the zone's pitch wheel: (v − 0.5) · 2400 cents
        z.pitch = std::clamp (0.5f + toF (r.get ("tune"), toF (r.get ("pitch"), 0.0f)) / 2400.0f, 0.0f, 1.0f);
        // volume (dB) on the gain wheel, whose gain is v²
        const auto db = toF (r.get ("volume"), 0.0f) + toF (r.get ("gain"), 0.0f);
        z.gain = std::clamp ((float) std::sqrt (std::pow (10.0, db / 20.0)), 0.0f, 1.0f);
        z.pan = std::clamp (0.5f + toF (r.get ("pan"), 0.0f) / 200.0f, 0.0f, 1.0f);
        z.offset = std::max (0, (int) toF (r.get ("offset"), 0.0f));
        const auto mode = r.get ("loop_mode", r.get ("loopmode"));
        z.loopMode = mode == "one_shot" ? 1 : mode == "loop_continuous" ? 2 : mode == "loop_sustain" ? 3 : mode == "no_loop" ? 0 : -1;
        const auto ls = r.get ("loop_start", r.get ("loopstart")), le = r.get ("loop_end", r.get ("loopend"));
        if (! ls.empty() && ! le.empty())
        {
            z.loopA = (int) toF (ls, 0.0f);
            z.loopB = (int) toF (le, 0.0f) + 1;
            if (z.loopMode < 0)
                z.loopMode = 2;
        }
        p.zones.push_back (std::move (z)); // (loopMode −1: the file's own loop, filled with its format)
    }
    return p;
}
} // namespace lynx::dw
