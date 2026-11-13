#include "core/Rack.h"

#include <algorithm>
#include <cmath>
#include <memory>

#include "core/Effects.h"
#include "core/NoteEdit.h"

namespace lynx::rack
{
// ---------------------------------------------------------------------------------------------------------------
// Laws

// Live's chain-volume taper (REA §4 [DATA]): 0 dB at 0.85, 40 dB per unit from −18 dB (0.4) up to +6 dB (1); below, the
// stored points of Live's own racks, linear in dB between them, down to −70 dB, Live's floor (shown as −inf) at 0.
namespace
{
constexpr float taperT[] { 0.0f, 0.143f, 0.22f, 0.282f, 0.329f, 0.36f, 0.4f };
constexpr float taperDb[] { -70.0f, -42.0f, -32.0f, -25.69f, -22.0f, -20.0f, -18.0f };
constexpr int taperPoints = 7;
} // namespace

float volumeGain (float v) noexcept
{
    v = std::clamp (v, 0.0f, 1.0f);
    if (v <= 0.0f)
        return 0.0f;
    float db = 0.0f;
    if (v >= 0.4f)
        db = 40.0f * (v - unityVolume);
    else
        for (int i = 1; i < taperPoints; ++i)
            if (v <= taperT[i])
            {
                const auto f = (v - taperT[i - 1]) / (taperT[i] - taperT[i - 1]);
                db = taperDb[i - 1] + (taperDb[i] - taperDb[i - 1]) * f;
                break;
            }
    return std::pow (10.0f, db / 20.0f);
}

float volumeValue (float gain) noexcept
{
    if (! (gain > 0.0003163f)) // (Live's floor, −70 dB, is the bottom)
        return 0.0f;
    const auto db = 20.0f * std::log10 (gain);
    if (db >= -18.0f)
        return std::clamp (unityVolume + db / 40.0f, 0.0f, 1.0f);
    for (int i = 1; i < taperPoints; ++i)
        if (db <= taperDb[i])
            return taperT[i - 1] + (taperT[i] - taperT[i - 1]) * (db - taperDb[i - 1]) / (taperDb[i] - taperDb[i - 1]);
    return 0.4f;
}

void panGains (float pan, float& left, float& right) noexcept
{
    // [RENDER] REA §8: equal power with unity at the centre, +3 dB on the near side at the ends (−0.5: +2.32 / −5.33 dB)
    const auto theta = (std::clamp (pan, -1.0f, 1.0f) + 1.0f) * 0.78539816f;
    left = 1.41421356f * std::cos (theta);
    right = 1.41421356f * std::sin (theta);
    if (std::abs (pan) < 1.0e-6f)
        left = right = 1.0f;
}

bool inZone (const RackZone& z, float x) noexcept { return x >= (float) z.lo && x <= (float) z.hi; }

float fadeGain (const RackZone& z, float x) noexcept
{
    // [RENDER] REA §8: linear, counting both ends: x = lo gives 1 / (fadeLo − lo + 1), fadeLo gives 1
    if (! inZone (z, x))
        return 0.0f;
    if (x < (float) z.fadeLo && z.fadeLo > z.lo)
        return std::clamp ((x - (float) z.lo + 1.0f) / (float) (z.fadeLo - z.lo + 1), 0.0f, 1.0f);
    if (x > (float) z.fadeHi && z.hi > z.fadeHi)
        return std::clamp (((float) z.hi - x + 1.0f) / (float) (z.hi - z.fadeHi + 1), 0.0f, 1.0f);
    return 1.0f;
}

bool fadesTowards (const RackZone& z, float x) noexcept
{
    return x < (float) z.lo ? z.fadeLo > z.lo : x > (float) z.hi ? z.hi > z.fadeHi : false;
}

float selectOutputGain (const RackZone& z, float x) noexcept
{
    if (inZone (z, x))
        return std::sin (1.57079633f * fadeGain (z, x)); // [RENDER] REA §8: sin (π/2 · the note fade), within 0.001
    return fadesTowards (z, x) ? 0.0f : 1.0f; // (no fade on that side: the output rings out, the input is closed)
}

bool selectInputOpen (const RackZone& z, float x) noexcept { return inZone (z, x); }

float zoneSelector (const PatchZone& zone, float surfaceValue) noexcept
{
    const auto v = zone.selectorMin + (zone.selectorMax - zone.selectorMin) * std::clamp (surfaceValue, 0.0f, 1.0f);
    return std::clamp (v, 0.0f, 1.0f) * 127.0f;
}

float zoneVelocity (const PatchZone& zone, int key, float velocity, float selector) noexcept
{
    const auto v127 = std::clamp (velocity * 127.0f, 1.0f, 127.0f);
    auto gain = fadeGain (zone.key, (float) key) * fadeGain (zone.velocity, std::round (v127));
    if (zone.selector >= 0)
    {
        if (! inZone (zone.select, selector))
            return 0.0f;
        if (zone.selectorScalesVelocity)
            gain *= fadeGain (zone.select, selector);
    }
    if (gain <= 0.0f)
        return 0.0f;
    // [RENDER] REA §8: the velocity is scaled, not rounded, and a note that enters keeps a velocity of at least 1
    return std::max (1.0f / 127.0f, velocity * gain);
}

// ---------------------------------------------------------------------------------------------------------------
// Documents

namespace
{
bool isMidiEffectNode (const PatchNode& n) noexcept { return n.generator && n.channel.instrument == InstrumentKind::vfx; }
bool isInstrumentNode (const PatchNode& n) noexcept { return n.generator && n.channel.instrument != InstrumentKind::vfx; }

PatchNode mixerNode()
{
    PatchNode n;
    n.effect = makeEffectSlot (EffectKind::rackMixer);
    n.label = "Chain mixer";
    return n;
}

int midiPortOfMidiRack() noexcept { return 0; }

// shifts every rack index above `node` down by one (the node was erased from the Patcher)
void forgetNode (RackDoc& r, int node)
{
    for (auto* list : { &r.chains, &r.returns })
        for (auto& c : *list)
    {
        std::erase (c.nodes, node);
        for (auto& n : c.nodes)
            if (n > node)
                --n;
        for (auto* k : { &c.mixer, &c.gate })
            *k = *k == node ? -1 : (*k > node ? *k - 1 : *k);
    }
    for (auto& m : r.macros)
    {
        std::erase_if (m.mappings, [node] (const RackMapping& x) { return x.target == RackMapping::Target::param && x.node == node; });
        for (auto& x : m.mappings)
            if (x.target == RackMapping::Target::param && x.node > node)
                --x.node;
    }
}

void eraseNode (PatcherDoc& doc, int node)
{
    if (node < 0 || node >= (int) doc.nodes.size())
        return;
    doc.nodes.erase (doc.nodes.begin() + node);
    doc.wires.clear(); // (compile() builds them again)
    if (auto* r = rackOf (doc))
        forgetNode (*r, node);
}

int newNode (PatcherDoc& doc, PatchNode node)
{
    doc.nodes.push_back (std::move (node));
    return (int) doc.nodes.size() - 1;
}

void addChainNodes (PatcherDoc& doc, RackChain& chain, RackDoc::Kind kind)
{
    if (kind == RackDoc::Kind::midiEffect)
        return;
    if (chain.mixer < 0)
        chain.mixer = newNode (doc, mixerNode());
    if (kind == RackDoc::Kind::audioEffect && chain.gate < 0)
    {
        auto gate = mixerNode();
        gate.label = "Chain input";
        chain.gate = newNode (doc, std::move (gate));
    }
}

std::vector<float> sampledCurve (int points, auto&& f)
{
    std::vector<float> curve ((std::size_t) points);
    for (int i = 0; i < points; ++i)
        curve[(std::size_t) i] = std::clamp (f ((float) i / (float) (points - 1)), 0.0f, 1.0f);
    return curve;
}

// The selector's source on the Surface and its range there: its own control, or the macro mapped onto it
void selectorSource (const RackDoc& r, int& output, float& lo, float& hi)
{
    output = selectorOutput;
    lo = 0.0f;
    hi = 1.0f;
    for (int m = 0; m < rackMacroCount; ++m)
        for (const auto& x : r.macros[(std::size_t) m].mappings)
            if (x.target == RackMapping::Target::selector)
            {
                output = m;
                lo = x.min;
                hi = x.max;
                return;
            }
}

bool morphing (const RackDoc& r, int macro) noexcept
{
    if (! r.morphOn || r.morphA < 0 || r.morphB < 0 || r.morphA >= (int) r.variations.size() || r.morphB >= (int) r.variations.size())
        return false;
    const auto& a = r.variations[(std::size_t) r.morphA];
    const auto& b = r.variations[(std::size_t) r.morphB];
    return a.has[(std::size_t) macro] && b.has[(std::size_t) macro] && ! r.macros[(std::size_t) macro].excludeFromVariations;
}
} // namespace

const RackDoc* rackOf (const PatcherDoc& doc) noexcept { return doc.rack.empty() ? nullptr : &doc.rack.front(); }
RackDoc* rackOf (PatcherDoc& doc) noexcept { return doc.rack.empty() ? nullptr : &doc.rack.front(); }
bool isRack (const EffectSlot& slot) noexcept { return slot.kind == EffectKind::patcher && slot.patcher.isRack(); }
bool isRack (const ChannelDef& channel) noexcept { return channel.instrument == InstrumentKind::patcher && channel.patcher.isRack(); }

PatcherDoc makeRack (RackDoc::Kind kind)
{
    PatcherDoc doc;
    RackDoc r;
    r.kind = kind;
    r.showMacros = true;
    doc.rack.push_back (std::move (r));
    addChain (doc);
    return doc;
}

EffectSlot makeRackSlot (RackDoc::Kind kind)
{
    auto slot = makeEffectSlot (EffectKind::patcher);
    slot.patcher = makeRack (kind);
    return slot;
}

const std::vector<int>& devices (const RackDoc& rack, int chain)
{
    static const std::vector<int> none;
    const auto* c = chainAt (rack, chain);
    return c != nullptr ? c->nodes : none;
}

bool isChainNode (const RackDoc& rack, int node) noexcept
{
    for (const auto* list : { &rack.chains, &rack.returns })
        for (const auto& c : *list)
            if (c.mixer == node || c.gate == node)
                return true;
    return false;
}

void compile (PatcherDoc& doc)
{
    auto* r = rackOf (doc);
    if (r == nullptr)
        return;
    doc.wires.clear();
    doc.zones.clear();
    doc.published.clear();
    doc.fromMidiPorts.clear();
    doc.toMidiPorts.clear();
    doc.outputTracks.clear();

    // the Surface: 16 macros, the chain selector, the morph (their values; the knobs are the rack's own UI)
    doc.surface.assign ((std::size_t) surfaceOutputs, SurfaceControl {});
    for (int k = 0; k < surfaceOutputs; ++k)
    {
        auto& s = doc.surface[(std::size_t) k];
        s.kind = SurfaceControl::Kind::knob;
        s.x = 40.0f + 60.0f * (float) (k % 9);
        s.y = 30.0f + 60.0f * (float) (k / 9);
        if (k < rackMacroCount)
        {
            s.name = macroName (*r, k);
            s.value = std::clamp (r->macros[(std::size_t) k].value / 127.0f, 0.0f, 1.0f);
        }
        else if (k == selectorOutput)
        {
            s.name = "Chain Selector";
            s.value = std::clamp (r->selector / 127.0f, 0.0f, 1.0f);
        }
        else
        {
            s.name = "Morph";
            s.value = std::clamp (r->morph, 0.0f, 1.0f);
        }
        // automation: published parameter k sets Surface control k (one lane moves every mapping of a macro)
        PatchParam p;
        p.node = patchSurface;
        p.param = k;
        p.name = s.name;
        doc.published.push_back (std::move (p));
    }

    int selOut = selectorOutput;
    float selLo = 0.0f, selHi = 1.0f;
    selectorSource (*r, selOut, selLo, selHi);
    const auto selectorAt = [&] (float x) { return std::clamp (selLo + (selHi - selLo) * x, 0.0f, 1.0f) * 127.0f; };

    const auto count = (int) doc.nodes.size();
    const auto valid = [count] (int n) { return n >= 0 && n < count; };
    bool anySolo = false;
    for (const auto& c : r->chains)
        anySolo = anySolo || c.solo;
    // a Drum Rack's return chains (round 190ec): where a chain's audio and sends go in (the first effect, else the mixer)
    for (auto& ret : r->returns)
        std::erase_if (ret.nodes, [&] (int n) { return ! valid (n) || doc.nodes[(std::size_t) n].generator; });
    const auto returnHead = [&] (int k) {
        if (! r->drum || k < 0 || k >= (int) r->returns.size())
            return -1;
        const auto& ret = r->returns[(std::size_t) k];
        return ! ret.nodes.empty() ? ret.nodes.front() : valid (ret.mixer) ? ret.mixer : -1;
    };

    for (int ci = 0; ci < (int) r->chains.size(); ++ci)
    {
        auto& c = r->chains[(std::size_t) ci];
        const auto audible = c.active && (! anySolo || c.solo);
        std::erase_if (c.nodes, [&] (int n) { return ! valid (n); });

        // places on the Map (for anyone who opens it as a Patcher)
        const auto y = 60.0f + 90.0f * (float) ci;
        float x = 150.0f;
        auto place = [&] (int n) {
            if (valid (n))
            {
                doc.nodes[(std::size_t) n].x = x;
                doc.nodes[(std::size_t) n].y = y;
                x += 130.0f;
            }
        };

        if (r->kind == RackDoc::Kind::audioEffect)
        {
            int previous = patchFromHost;
            auto link = [&] (int n) {
                PatchWire w;
                w.kind = PatchWire::Kind::audio;
                w.from = previous;
                w.to = n;
                doc.wires.push_back (w);
                previous = n;
            };
            if (valid (c.gate))
            {
                place (c.gate);
                link (c.gate);
                auto& gate = doc.nodes[(std::size_t) c.gate].effect;
                gate.params[(std::size_t) mixerVolume] = unityVolume;
                gate.params[(std::size_t) mixerPan] = 0.5f;
                PatchWire sel;
                sel.kind = PatchWire::Kind::control;
                sel.from = patchSurface;
                sel.fromPort = selOut;
                sel.to = c.gate;
                sel.toPort = mixerSelect;
                sel.curve = sampledCurve (129, [&] (float v) { return selectInputOpen (c.select, selectorAt (v)) ? 1.0f : 0.0f; });
                doc.wires.push_back (std::move (sel));
            }
            for (const auto n : c.nodes)
                if (! doc.nodes[(std::size_t) n].generator)
                {
                    place (n);
                    link (n);
                }
            if (valid (c.mixer))
            {
                place (c.mixer);
                link (c.mixer);
            }
            PatchWire out;
            out.kind = PatchWire::Kind::audio;
            out.from = previous;
            out.to = patchToHost;
            out.enabled = audible;
            doc.wires.push_back (out);
        }
        else
        {
            // notes: From LYNX → (the chain's zones) → MIDI effects → the instrument; audio: instrument → effects → mixer
            PatchZone z;
            z.key = c.key;
            z.velocity = c.velocity;
            z.select = c.select;
            z.selector = selOut;
            z.selectorMin = selLo;
            z.selectorMax = selHi;
            z.selectorScalesVelocity = r->kind == RackDoc::Kind::midiEffect;
            z.selectorFiltersCtrl = r->selectorFiltersCtrl;
            if (r->drum) // a pad (round 190ec): the Receive note in, the Play note to the devices, its choke group
            {
                const auto receive = std::clamp (c.receive, -1, 127);
                z.key = receive < 0 ? RackZone {} : RackZone { receive, receive, receive, receive };
                c.key = z.key; // (what a LYNX without Drum Racks plays: a key split on the pad's note)
                z.playKey = receive < 0 ? -1 : std::clamp (c.play, 0, 127);
                z.choke = std::clamp (c.choke, 0, drumChokeGroups);
            }
            const auto zone = (int) doc.zones.size();
            doc.zones.push_back (z);

            int noteFrom = patchFromHost, notePin = 0;
            bool first = true;
            int instrument = -1;
            for (const auto n : c.nodes)
            {
                const auto& node = doc.nodes[(std::size_t) n];
                if (! node.generator)
                    continue;
                place (n);
                PatchWire w;
                w.kind = PatchWire::Kind::midi;
                w.from = noteFrom;
                w.fromPort = notePin;
                w.to = n;
                w.enabled = c.active;
                w.zone = first ? zone : -1;
                doc.wires.push_back (w);
                first = false;
                if (isMidiEffectNode (node))
                {
                    noteFrom = n;
                    notePin = 0;
                }
                else
                {
                    instrument = n;
                    break;
                }
            }
            if (r->drum)
                doc.zones[(std::size_t) zone].chokeNode = instrument;
            if (r->kind == RackDoc::Kind::midiEffect)
            {
                if (noteFrom != patchFromHost) // the last MIDI effect plays on "To LYNX" port 0 (the rack's children)
                {
                    PatchWire w;
                    w.kind = PatchWire::Kind::midi;
                    w.from = noteFrom;
                    w.fromPort = 0;
                    w.to = patchToHost;
                    w.toPort = midiPortOfMidiRack();
                    w.enabled = audible;
                    doc.wires.push_back (w);
                    if (std::find (doc.toMidiPorts.begin(), doc.toMidiPorts.end(), midiPortOfMidiRack()) == doc.toMidiPorts.end())
                        doc.toMidiPorts.push_back (midiPortOfMidiRack());
                }
                continue;
            }
            int previous = instrument;
            if (previous >= 0)
                for (const auto n : c.nodes)
                    if (! doc.nodes[(std::size_t) n].generator)
                    {
                        place (n);
                        PatchWire w;
                        w.kind = PatchWire::Kind::audio;
                        w.from = previous;
                        w.to = n;
                        doc.wires.push_back (w);
                        previous = n;
                    }
            if (valid (c.mixer))
            {
                place (c.mixer);
                if (previous >= 0)
                {
                    PatchWire w;
                    w.kind = PatchWire::Kind::audio;
                    w.from = previous;
                    w.to = c.mixer;
                    doc.wires.push_back (w);
                }
                PatchWire out;
                out.kind = PatchWire::Kind::audio;
                out.from = c.mixer;
                out.to = r->drum && returnHead (c.audioTo) >= 0 ? returnHead (c.audioTo) : patchToHost;
                out.enabled = audible;
                doc.wires.push_back (out);
                // a Drum Rack chain's sends: one cable per return chain after the chain's mixer (post its volume and pan),
                // its gain the send level; a send at −inf or switched off is a disabled cable (no rebuild when it moves)
                if (r->drum)
                {
                    c.sends.resize (r->returns.size());
                    for (int k = 0; k < (int) r->returns.size(); ++k)
                        if (const auto head = returnHead (k); head >= 0)
                        {
                            const auto& send = c.sends[(std::size_t) k];
                            PatchWire w;
                            w.kind = PatchWire::Kind::audio;
                            w.from = c.mixer;
                            w.to = head;
                            w.gain = std::max (0.0f, send.level);
                            w.enabled = audible && send.on && send.level > 0.0f;
                            doc.wires.push_back (w);
                        }
                }
            }
        }

        // the chain mixer: volume, pan, and the chain-select gain from the selector (or its macro)
        if (valid (c.mixer))
        {
            auto& mixer = doc.nodes[(std::size_t) c.mixer].effect;
            mixer.params[(std::size_t) mixerVolume] = volumeValue (c.volume);
            mixer.params[(std::size_t) mixerPan] = std::clamp ((c.pan + 1.0f) * 0.5f, 0.0f, 1.0f);
            mixer.params[(std::size_t) mixerSelect] = 1.0f;
            PatchWire sel;
            sel.kind = PatchWire::Kind::control;
            sel.from = patchSurface;
            sel.fromPort = selOut;
            sel.to = c.mixer;
            sel.toPort = mixerSelect;
            sel.curve = sampledCurve (129, [&] (float v) { return selectOutputGain (c.select, selectorAt (v)); });
            doc.wires.push_back (std::move (sel));
        }
    }

    // a Drum Rack's return chains: their effects in series, then their mixer, into the rack's output
    if (r->drum)
    {
        bool anyReturnSolo = false;
        for (const auto& ret : r->returns)
            anyReturnSolo = anyReturnSolo || ret.solo;
        for (int k = 0; k < (int) r->returns.size(); ++k)
        {
            auto& ret = r->returns[(std::size_t) k];
            if (! valid (ret.mixer))
                ret.mixer = -1;
            const auto y = 60.0f + 90.0f * (float) (r->chains.size() + (std::size_t) k);
            float x = 150.0f;
            int previous = -1;
            for (const auto n : ret.nodes)
            {
                doc.nodes[(std::size_t) n].x = x;
                doc.nodes[(std::size_t) n].y = y;
                x += 130.0f;
                if (previous >= 0)
                {
                    PatchWire w;
                    w.kind = PatchWire::Kind::audio;
                    w.from = previous;
                    w.to = n;
                    doc.wires.push_back (w);
                }
                previous = n;
            }
            if (ret.mixer < 0)
                continue;
            doc.nodes[(std::size_t) ret.mixer].x = x;
            doc.nodes[(std::size_t) ret.mixer].y = y;
            if (previous >= 0)
            {
                PatchWire w;
                w.kind = PatchWire::Kind::audio;
                w.from = previous;
                w.to = ret.mixer;
                doc.wires.push_back (w);
            }
            auto& mixer = doc.nodes[(std::size_t) ret.mixer].effect;
            mixer.params[(std::size_t) mixerVolume] = volumeValue (ret.volume);
            mixer.params[(std::size_t) mixerPan] = std::clamp ((ret.pan + 1.0f) * 0.5f, 0.0f, 1.0f);
            mixer.params[(std::size_t) mixerSelect] = 1.0f;
            PatchWire out;
            out.kind = PatchWire::Kind::audio;
            out.from = ret.mixer;
            out.to = patchToHost;
            out.enabled = ret.active && (! anyReturnSolo || ret.solo);
            doc.wires.push_back (out);
        }
    }

    // macro mappings: a control cable each, the curve {min, max} (Live's linear law in the target's 0..1 range); with
    // the morph on, a morphing macro's cables come from the Morph knob (A's value → B's)
    for (int m = 0; m < rackMacroCount; ++m)
        for (const auto& x : r->macros[(std::size_t) m].mappings)
        {
            PatchWire w;
            w.kind = PatchWire::Kind::control;
            w.from = patchSurface;
            w.fromPort = m;
            w.curve = { std::clamp (x.min, 0.0f, 1.0f), std::clamp (x.max, 0.0f, 1.0f) };
            if (morphing (*r, m))
            {
                const auto a = r->variations[(std::size_t) r->morphA].values[(std::size_t) m];
                const auto b = r->variations[(std::size_t) r->morphB].values[(std::size_t) m];
                w.fromPort = morphOutput;
                w.curve = { mappedValue (x, a), mappedValue (x, b) };
            }
            switch (x.target)
            {
                case RackMapping::Target::param:
                    if (! valid (x.node) || x.param < 0)
                        continue;
                    w.to = x.node;
                    w.toPort = x.param;
                    break;
                case RackMapping::Target::chainVolume:
                case RackMapping::Target::chainPan:
                    if (x.chain < 0 || x.chain >= (int) r->chains.size() || ! valid (r->chains[(std::size_t) x.chain].mixer))
                        continue;
                    w.to = r->chains[(std::size_t) x.chain].mixer;
                    w.toPort = x.target == RackMapping::Target::chainVolume ? mixerVolume : mixerPan;
                    break;
                case RackMapping::Target::selector: continue; // (composed into the selector's cables above)
            }
            doc.wires.push_back (std::move (w));
        }
}

// ---------------------------------------------------------------------------------------------------------------
// Chains and devices

int addChain (PatcherDoc& doc, const std::string& name)
{
    auto* r = rackOf (doc);
    if (r == nullptr)
        return -1;
    RackChain c;
    c.name = name.empty() ? "Chain " + std::to_string (r->chains.size() + 1) : name;
    addChainNodes (doc, c, r->kind);
    r = rackOf (doc);
    r->chains.push_back (std::move (c));
    r->selectedChain = (int) r->chains.size() - 1;
    compile (doc);
    return (int) r->chains.size() - 1;
}

bool removeChain (PatcherDoc& doc, int chain)
{
    auto* r = rackOf (doc);
    if (r == nullptr || chain < 0 || chain >= (int) r->chains.size())
        return false;
    auto nodes = r->chains[(std::size_t) chain].nodes;
    for (const auto k : { r->chains[(std::size_t) chain].mixer, r->chains[(std::size_t) chain].gate })
        if (k >= 0)
            nodes.push_back (k);
    for (auto& m : r->macros) // its volume / pan mappings go; the later chains' move down
    {
        std::erase_if (m.mappings, [chain] (const RackMapping& x) { return x.target != RackMapping::Target::param && x.target != RackMapping::Target::selector && x.chain == chain; });
        for (auto& x : m.mappings)
            if (x.target != RackMapping::Target::param && x.chain > chain)
                --x.chain;
    }
    r->chains.erase (r->chains.begin() + chain);
    std::sort (nodes.rbegin(), nodes.rend());
    for (const auto n : nodes)
        eraseNode (doc, n);
    r = rackOf (doc);
    r->selectedChain = std::clamp (r->selectedChain, 0, std::max (0, (int) r->chains.size() - 1));
    compile (doc);
    return true;
}

int duplicateChain (PatcherDoc& doc, int chain)
{
    auto* r = rackOf (doc);
    if (r == nullptr || chain < 0 || chain >= (int) r->chains.size())
        return -1;
    auto copy = r->chains[(std::size_t) chain];
    std::vector<std::pair<int, int>> moved; // old node → new node (for the mappings, which a copy keeps as Live does)
    for (auto& n : copy.nodes)
    {
        auto node = doc.nodes[(std::size_t) n];
        node.effect.instance = {};
        const auto now = newNode (doc, std::move (node));
        moved.emplace_back (n, now);
        n = now;
    }
    copy.mixer = copy.gate = -1;
    copy.solo = false;
    r = rackOf (doc);
    addChainNodes (doc, copy, r->kind);
    r = rackOf (doc);
    for (auto& m : r->macros)
    {
        const auto before = m.mappings.size();
        for (std::size_t i = 0; i < before; ++i)
        {
            auto x = m.mappings[i];
            if (x.target != RackMapping::Target::param)
                continue;
            for (const auto& [from, to] : moved)
                if (x.node == from)
                {
                    x.node = to;
                    m.mappings.push_back (x);
                }
        }
    }
    r->chains.insert (r->chains.begin() + chain + 1, std::move (copy));
    r->selectedChain = chain + 1;
    compile (doc);
    return chain + 1;
}

bool moveChain (PatcherDoc& doc, int from, int to)
{
    auto* r = rackOf (doc);
    if (r == nullptr || from < 0 || from >= (int) r->chains.size() || to < 0 || to >= (int) r->chains.size() || from == to)
        return false;
    auto c = std::move (r->chains[(std::size_t) from]);
    r->chains.erase (r->chains.begin() + from);
    r->chains.insert (r->chains.begin() + to, std::move (c));
    for (auto& m : r->macros)
        for (auto& x : m.mappings)
            if (x.target == RackMapping::Target::chainVolume || x.target == RackMapping::Target::chainPan)
            {
                if (x.chain == from)
                    x.chain = to;
                else if (from < to && x.chain > from && x.chain <= to)
                    --x.chain;
                else if (to < from && x.chain >= to && x.chain < from)
                    ++x.chain;
            }
    r->selectedChain = to;
    compile (doc);
    return true;
}

int addDevice (PatcherDoc& doc, int chain, PatchNode node, int position)
{
    auto* r = rackOf (doc);
    if (r == nullptr || chainAt (*r, chain) == nullptr)
        return -1;
    // what each rack kind holds (Live, REA §1): audio effects only; MIDI effects only; MIDI effects, one instrument,
    // audio effects (a Drum Rack's return chain: audio effects only)
    const auto midi = isMidiEffectNode (node), instrument = isInstrumentNode (node);
    if ((r->kind == RackDoc::Kind::audioEffect && node.generator) || (r->kind == RackDoc::Kind::midiEffect && ! midi)
        || (isReturnSlot (*r, chain) && node.generator))
        return -1;
    auto& list = chainAt (*r, chain)->nodes;
    auto rankOf = [&] (const PatchNode& n) { return isMidiEffectNode (n) ? 0 : isInstrumentNode (n) ? 1 : 2; };
    if (instrument && std::any_of (list.begin(), list.end(), [&] (int n) { return isInstrumentNode (doc.nodes[(std::size_t) n]); }))
        return -1; // (one instrument per chain)
    const auto rank = rankOf (node);
    const auto size = (int) list.size();
    position = position < 0 || position > size ? size : position;
    // the first place that keeps the order: after every device of a lower rank, before every one of a higher rank
    int lo = 0, hi = size;
    for (int i = 0; i < size; ++i)
    {
        const auto k = rankOf (doc.nodes[(std::size_t) list[(std::size_t) i]]);
        if (k < rank)
            lo = i + 1;
        if (k > rank && hi == size)
            hi = i;
    }
    position = std::clamp (position, lo, std::max (lo, hi));
    const auto index = newNode (doc, std::move (node));
    r = rackOf (doc);
    auto& nodes = chainAt (*r, chain)->nodes;
    nodes.insert (nodes.begin() + position, index);
    compile (doc);
    return index;
}

bool removeDevice (PatcherDoc& doc, int chain, int position)
{
    auto* r = rackOf (doc);
    const auto* c = r != nullptr ? chainAt (*r, chain) : nullptr;
    if (c == nullptr || position < 0 || position >= (int) c->nodes.size())
        return false;
    eraseNode (doc, c->nodes[(std::size_t) position]);
    compile (doc);
    return true;
}

bool moveDevice (PatcherDoc& doc, int chain, int from, int to)
{
    auto* r = rackOf (doc);
    if (r == nullptr || chainAt (*r, chain) == nullptr)
        return false;
    auto& list = chainAt (*r, chain)->nodes;
    if (from < 0 || from >= (int) list.size() || to < 0 || to >= (int) list.size() || from == to)
        return false;
    const auto n = list[(std::size_t) from];
    const auto& node = doc.nodes[(std::size_t) n];
    // an Instrument Rack's order is kept: a device moves only among its own kind
    const auto lo = (int) std::count_if (list.begin(), list.end(), [&] (int k) {
        const auto& d = doc.nodes[(std::size_t) k];
        return isMidiEffectNode (node) ? false : isInstrumentNode (node) ? isMidiEffectNode (d) : d.generator;
    });
    const auto same = (int) std::count_if (list.begin(), list.end(), [&] (int k) {
        const auto& d = doc.nodes[(std::size_t) k];
        return isMidiEffectNode (d) == isMidiEffectNode (node) && d.generator == node.generator;
    });
    to = std::clamp (to, lo, lo + same - 1);
    if (to == from)
        return false;
    list.erase (list.begin() + from);
    list.insert (list.begin() + to, n);
    compile (doc);
    return true;
}

void setSolo (PatcherDoc& doc, int chain, bool solo)
{
    if (auto* r = rackOf (doc); r != nullptr && chainAt (*r, chain) != nullptr)
    {
        chainAt (*r, chain)->solo = solo;
        compile (doc);
    }
}

void setActive (PatcherDoc& doc, int chain, bool active)
{
    if (auto* r = rackOf (doc); r != nullptr && chainAt (*r, chain) != nullptr)
    {
        chainAt (*r, chain)->active = active;
        compile (doc);
    }
}

void setChainVolume (PatcherDoc& doc, int chain, float gain)
{
    if (auto* r = rackOf (doc); r != nullptr && chainAt (*r, chain) != nullptr)
    {
        chainAt (*r, chain)->volume = std::clamp (gain, 0.0f, volumeGain (1.0f));
        compile (doc);
    }
}

void setChainPan (PatcherDoc& doc, int chain, float pan)
{
    if (auto* r = rackOf (doc); r != nullptr && chainAt (*r, chain) != nullptr)
    {
        chainAt (*r, chain)->pan = std::clamp (pan, -1.0f, 1.0f);
        compile (doc);
    }
}

void setSelector (PatcherDoc& doc, float value)
{
    if (auto* r = rackOf (doc))
    {
        r->selector = std::clamp (value, 0.0f, 127.0f);
        if ((std::size_t) selectorOutput < doc.surface.size())
            doc.surface[(std::size_t) selectorOutput].value = r->selector / 127.0f;
        else
            compile (doc);
    }
}

EffectSlot group (const std::vector<EffectSlot>& slots)
{
    auto slot = makeRackSlot (RackDoc::Kind::audioEffect);
    for (const auto& s : slots)
    {
        PatchNode n;
        n.effect = s;
        n.effect.row = -1;
        n.effect.instance = {};
        addDevice (slot.patcher, 0, std::move (n));
    }
    return slot;
}

bool fromPatcher (PatcherDoc& doc, const std::function<float (int node, int param)>& pluginValue)
{
    if (doc.isRack() || doc.nodes.empty() || doc.published.size() > (std::size_t) rackMacroCount || ! doc.surface.empty())
        return false;
    // a plugin's parameter: its current value, or no grouping (a macro at 0 would move it)
    std::vector<float> pluginValues (doc.published.size(), -1.0f);
    for (std::size_t k = 0; k < doc.published.size(); ++k)
        if (const auto& p = doc.published[k]; p.node >= 0 && p.node < (int) doc.nodes.size() && doc.nodes[(std::size_t) p.node].effect.kind == EffectKind::plugin)
        {
            pluginValues[k] = pluginValue ? pluginValue (p.node, p.param) : -1.0f;
            if (! (pluginValues[k] >= 0.0f))
                return false;
        }
    for (const auto& n : doc.nodes)
        if (n.generator || ! n.busHeads.empty())
            return false;
    for (const auto& p : doc.published)
        if (p.curve.size() > 2 || (p.node >= 0 && p.node >= (int) doc.nodes.size()))
            return false;
    // the series: From LYNX → a → b → … → To LYNX, every node once, nothing else
    std::vector<int> order;
    int at = patchFromHost;
    for (std::size_t guard = 0; guard <= doc.nodes.size(); ++guard)
    {
        const PatchWire* next = nullptr;
        int count = 0;
        for (const auto& w : doc.wires)
            if (w.kind == PatchWire::Kind::audio && w.from == at && w.fromPort == 0 && w.enabled)
            {
                next = &w;
                ++count;
            }
        if (count != 1 || next->toPort != 0 || next->gain != 1.0f)
            return false;
        if (next->to == patchToHost)
            break;
        order.push_back (next->to);
        at = next->to;
    }
    if (order.size() != doc.nodes.size() || doc.wires.size() != doc.nodes.size() + 1)
        return false;
    const auto published = doc.published;
    RackDoc r;
    r.kind = RackDoc::Kind::audioEffect;
    r.showDevices = true;
    doc.rack.push_back (std::move (r));
    RackChain chain;
    chain.name = "Chain 1";
    chain.nodes = order;
    chain.select = { 0, 127, 0, 127 };
    addChainNodes (doc, chain, RackDoc::Kind::audioEffect);
    auto* rk = rackOf (doc);
    rk->chains.push_back (std::move (chain));
    for (int k = 0; k < (int) published.size(); ++k)
    {
        const auto& p = published[(std::size_t) k];
        if (p.node < 0)
            continue;
        RackMapping m;
        m.node = p.node;
        m.param = p.param;
        if (p.curve.size() == 2)
            m.min = p.curve[0], m.max = p.curve[1];
        auto& macro = rk->macros[(std::size_t) k];
        macro.name = p.name;
        macro.mappings.push_back (m);
        const auto& node = doc.nodes[(std::size_t) p.node];
        const auto now = node.effect.kind == EffectKind::plugin ? pluginValues[(std::size_t) k]
                         : p.param >= 0 && p.param < maxEffectParams ? node.effect.params[(std::size_t) p.param] : 0.0f;
        if (m.max != m.min)
            macro.value = std::clamp ((now - m.min) / (m.max - m.min), 0.0f, 1.0f) * 127.0f;
    }
    compile (doc);
    return true;
}

std::vector<EffectSlot> ungroup (const EffectSlot& rackSlot)
{
    std::vector<EffectSlot> out;
    const auto* r = rackOf (rackSlot.patcher);
    if (! isRack (rackSlot) || r == nullptr || r->kind != RackDoc::Kind::audioEffect || r->chains.size() != 1)
        return out;
    for (const auto n : r->chains.front().nodes)
        if (n >= 0 && n < (int) rackSlot.patcher.nodes.size())
            out.push_back (rackSlot.patcher.nodes[(std::size_t) n].effect);
    return out;
}

int ungroupSlot (Song& song, int track, int slot)
{
    if (track < 0 || track >= (int) song.mixer.size())
        return -1;
    auto& fx = song.mixer[(std::size_t) track].effects;
    if (slot < 0 || slot >= (int) fx.size())
        return -1;
    const auto rackSlot = fx[(std::size_t) slot];
    const auto* r = rackOf (rackSlot.patcher);
    if (! isRack (rackSlot) || r == nullptr || r->kind != RackDoc::Kind::audioEffect || r->chains.size() != 1)
        return -1;
    const auto& nodes = r->chains.front().nodes;
    auto out = ungroup (rackSlot);
    std::vector<int> nodeAt; // device k's node
    for (const auto n : nodes)
        if (n >= 0 && n < (int) rackSlot.patcher.nodes.size())
            nodeAt.push_back (n);
    const auto devicePos = [&] (int node) {
        const auto it = std::find (nodeAt.begin(), nodeAt.end(), node);
        return it == nodeAt.end() ? -1 : (int) (it - nodeAt.begin());
    };
    // the macros' values onto the native parameters they move; the one-mapping full-range macros' new targets
    std::array<int, rackMacroCount> moveTo {}, moveParam {};
    moveTo.fill (-1);
    for (int m = 0; m < rackMacroCount; ++m)
    {
        const auto& macro = r->macros[(std::size_t) m];
        const auto u = std::clamp (macro.value / 127.0f, 0.0f, 1.0f);
        for (const auto& x : macro.mappings)
        {
            if (x.target != RackMapping::Target::param)
                continue;
            const auto k = devicePos (x.node);
            if (k < 0 || x.param < 0)
                continue;
            auto& dev = out[(std::size_t) k];
            if (dev.kind != EffectKind::plugin && x.param < maxEffectParams)
                dev.params[(std::size_t) x.param] = std::clamp (x.min + (x.max - x.min) * u, 0.0f, 1.0f);
        }
        if (macro.mappings.size() == 1 && macro.mappings[0].target == RackMapping::Target::param && macro.mappings[0].min == 0.0f &&
            macro.mappings[0].max == 1.0f && devicePos (macro.mappings[0].node) >= 0)
        {
            moveTo[(std::size_t) m] = slot + devicePos (macro.mappings[0].node);
            moveParam[(std::size_t) m] = macro.mappings[0].param;
        }
    }
    const auto added = (int) out.size() - 1; // later slots move by this much
    song.forEachTarget ([&] (AutomationTarget& t) {
        if ((t.kind != AutomationTargetKind::effectParam && t.kind != AutomationTargetKind::effectMix) || t.index != track)
            return;
        if (t.slot > slot)
            t.slot += added;
        else if (t.slot == slot)
        {
            if (t.kind == AutomationTargetKind::effectParam && t.param >= 0 && t.param < rackMacroCount && moveTo[(std::size_t) t.param] >= 0)
            {
                const auto m = (std::size_t) t.param;
                t.slot = moveTo[m];
                t.param = moveParam[m];
            }
            else if (t.kind != AutomationTargetKind::effectMix || out.size() != 1) // (one device keeps the slot's dry/wet)
                t.kind = AutomationTargetKind::none;
        }
    });
    for (auto& a : song.automations)
        if (a.source.track == track && a.source.slot >= 0)
        {
            if (a.source.slot > slot)
                a.source.slot += added;
            else if (a.source.slot == slot)
            {
                a.source.track = -1;
                a.target.kind = AutomationTargetKind::none;
            }
        }
    if (out.size() == 1)
        out[0].mix = rackSlot.mix * out[0].mix, out[0].enabled = rackSlot.enabled && out[0].enabled;
    else if (! rackSlot.enabled)
        for (auto& d : out)
            d.enabled = false;
    fx.erase (fx.begin() + slot);
    fx.insert (fx.begin() + slot, out.begin(), out.end());
    song.removeDeadMidiLinks();
    return slot;
}

// ---------------------------------------------------------------------------------------------------------------
// Macros

bool sameTarget (const RackMapping& a, const RackMapping& b) noexcept
{
    if (a.target != b.target)
        return false;
    switch (a.target)
    {
        case RackMapping::Target::param: return a.node == b.node && a.param == b.param;
        case RackMapping::Target::chainVolume:
        case RackMapping::Target::chainPan: return a.chain == b.chain;
        case RackMapping::Target::selector: return true;
    }
    return false;
}

int macroOf (const RackDoc& rack, const RackMapping& target, int* mappingIndex) noexcept
{
    for (int m = 0; m < rackMacroCount; ++m)
    {
        const auto& list = rack.macros[(std::size_t) m].mappings;
        for (int i = 0; i < (int) list.size(); ++i)
            if (sameTarget (list[(std::size_t) i], target))
            {
                if (mappingIndex != nullptr)
                    *mappingIndex = i;
                return m;
            }
    }
    return -1;
}

bool map (PatcherDoc& doc, int macro, const RackMapping& mapping)
{
    auto* r = rackOf (doc);
    if (r == nullptr || macro < 0 || macro >= rackMacroCount)
        return false;
    switch (mapping.target)
    {
        case RackMapping::Target::param:
            if (mapping.node < 0 || mapping.node >= (int) doc.nodes.size() || isChainNode (*r, mapping.node) || mapping.param < 0)
                return false;
            break;
        case RackMapping::Target::chainVolume:
        case RackMapping::Target::chainPan:
            if (mapping.chain < 0 || mapping.chain >= (int) r->chains.size() || r->kind == RackDoc::Kind::midiEffect)
                return false;
            break;
        case RackMapping::Target::selector: break;
    }
    int at = -1;
    if (const auto owner = macroOf (*r, mapping, &at); owner >= 0)
        r->macros[(std::size_t) owner].mappings.erase (r->macros[(std::size_t) owner].mappings.begin() + at);
    r->macros[(std::size_t) macro].mappings.push_back (mapping);
    compile (doc);
    return true;
}

bool unmap (PatcherDoc& doc, int macro, int mapping)
{
    auto* r = rackOf (doc);
    if (r == nullptr || macro < 0 || macro >= rackMacroCount || mapping < 0 || mapping >= (int) r->macros[(std::size_t) macro].mappings.size())
        return false;
    auto& list = r->macros[(std::size_t) macro].mappings;
    list.erase (list.begin() + mapping);
    compile (doc);
    return true;
}

bool invertRange (PatcherDoc& doc, int macro, int mapping)
{
    auto* r = rackOf (doc);
    if (r == nullptr || macro < 0 || macro >= rackMacroCount || mapping < 0 || mapping >= (int) r->macros[(std::size_t) macro].mappings.size())
        return false;
    auto& x = r->macros[(std::size_t) macro].mappings[(std::size_t) mapping];
    std::swap (x.min, x.max);
    compile (doc);
    return true;
}

float mappedValue (const RackMapping& m, float value) noexcept
{
    const auto t = std::clamp (value / 127.0f, 0.0f, 1.0f);
    return std::clamp (m.min + (m.max - m.min) * t, 0.0f, 1.0f);
}

void setMacro (PatcherDoc& doc, int macro, float value)
{
    auto* r = rackOf (doc);
    if (r == nullptr || macro < 0 || macro >= rackMacroCount)
        return;
    auto& m = r->macros[(std::size_t) macro];
    m.value = std::clamp (value, 0.0f, 127.0f);
    if ((std::size_t) macro < doc.surface.size())
        doc.surface[(std::size_t) macro].value = m.value / 127.0f;
    // what the macro drives, in the document too (the shown values, saving, export; Live stores them as well)
    for (const auto& x : m.mappings)
    {
        const auto v = mappedValue (x, m.value);
        switch (x.target)
        {
            case RackMapping::Target::param:
                if (x.node >= 0 && x.node < (int) doc.nodes.size() && ! doc.nodes[(std::size_t) x.node].generator && x.param < maxEffectParams
                    && doc.nodes[(std::size_t) x.node].effect.kind != EffectKind::plugin)
                    doc.nodes[(std::size_t) x.node].effect.params[(std::size_t) x.param] = v;
                break;
            case RackMapping::Target::chainVolume:
                if (x.chain >= 0 && x.chain < (int) r->chains.size())
                {
                    r->chains[(std::size_t) x.chain].volume = volumeGain (v);
                    if (const auto n = r->chains[(std::size_t) x.chain].mixer; n >= 0 && n < (int) doc.nodes.size())
                        doc.nodes[(std::size_t) n].effect.params[(std::size_t) mixerVolume] = v;
                }
                break;
            case RackMapping::Target::chainPan:
                if (x.chain >= 0 && x.chain < (int) r->chains.size())
                {
                    r->chains[(std::size_t) x.chain].pan = v * 2.0f - 1.0f;
                    if (const auto n = r->chains[(std::size_t) x.chain].mixer; n >= 0 && n < (int) doc.nodes.size())
                        doc.nodes[(std::size_t) n].effect.params[(std::size_t) mixerPan] = v;
                }
                break;
            case RackMapping::Target::selector: r->selector = v * 127.0f; break;
        }
    }
}

std::string macroName (const RackDoc& rack, int macro, const std::string& paramName)
{
    if (macro < 0 || macro >= rackMacroCount)
        return {};
    const auto& m = rack.macros[(std::size_t) macro];
    if (! m.name.empty())
        return m.name;
    if (m.mappings.size() == 1 && ! paramName.empty())
        return paramName;
    return "Macro " + std::to_string (macro + 1);
}

int randomize (PatcherDoc& doc, std::uint32_t& seed)
{
    auto* r = rackOf (doc);
    if (r == nullptr)
        return 0;
    int changed = 0;
    for (int m = 0; m < rackMacroCount; ++m)
    {
        const auto& macro = r->macros[(std::size_t) m];
        if (macro.mappings.empty() || macro.excludeFromRandom)
            continue;
        seed ^= seed << 13; // xorshift32
        seed ^= seed >> 17;
        seed ^= seed << 5;
        setMacro (doc, m, (float) (seed % 128u));
        r = rackOf (doc);
        ++changed;
    }
    return changed;
}

int moreMacros (int shown) noexcept { return shown < 2 ? 2 : std::min (rackMacroCount, (shown + 2) / 2 * 2); }
int fewerMacros (int shown) noexcept { return shown <= 2 ? 1 : (shown - 1) / 2 * 2; }

// ---------------------------------------------------------------------------------------------------------------
// Variations

namespace
{
void capture (const RackDoc& r, RackVariation& v)
{
    for (int m = 0; m < rackMacroCount; ++m)
    {
        const auto& macro = r.macros[(std::size_t) m];
        // the mapped macros get a value (INFERRED from Live's stored variations, REA §5); the others stay untouched
        v.has[(std::size_t) m] = ! macro.mappings.empty();
        v.values[(std::size_t) m] = v.has[(std::size_t) m] ? macro.value : -1.0f;
    }
}
} // namespace

int storeVariation (PatcherDoc& doc)
{
    auto* r = rackOf (doc);
    if (r == nullptr)
        return -1;
    RackVariation v;
    // Live names them "Variation 1", "Variation 2"… (the first number not taken)
    for (int n = 1;; ++n)
    {
        const auto name = "Variation " + std::to_string (n);
        if (std::none_of (r->variations.begin(), r->variations.end(), [&] (const RackVariation& x) { return x.name == name; }))
        {
            v.name = name;
            break;
        }
    }
    capture (*r, v);
    r->variations.push_back (std::move (v));
    r->selectedVariation = (int) r->variations.size() - 1;
    return r->selectedVariation;
}

bool overwriteVariation (PatcherDoc& doc, int index)
{
    auto* r = rackOf (doc);
    if (r == nullptr || index < 0 || index >= (int) r->variations.size())
        return false;
    capture (*r, r->variations[(std::size_t) index]);
    r->selectedVariation = index;
    compile (doc); // (a morph between it changes)
    return true;
}

bool launchVariation (PatcherDoc& doc, int index)
{
    auto* r = rackOf (doc);
    if (r == nullptr || index < 0 || index >= (int) r->variations.size())
        return false;
    const auto v = r->variations[(std::size_t) index];
    for (int m = 0; m < rackMacroCount; ++m)
        if (v.has[(std::size_t) m] && ! r->macros[(std::size_t) m].excludeFromVariations)
        {
            setMacro (doc, m, v.values[(std::size_t) m]);
            r = rackOf (doc);
        }
    r->selectedVariation = index;
    return true;
}

bool renameVariation (PatcherDoc& doc, int index, const std::string& name)
{
    auto* r = rackOf (doc);
    if (r == nullptr || index < 0 || index >= (int) r->variations.size() || name.empty())
        return false;
    r->variations[(std::size_t) index].name = name;
    return true;
}

int duplicateVariation (PatcherDoc& doc, int index)
{
    auto* r = rackOf (doc);
    if (r == nullptr || index < 0 || index >= (int) r->variations.size())
        return -1;
    auto copy = r->variations[(std::size_t) index];
    r->variations.insert (r->variations.begin() + index + 1, std::move (copy));
    for (auto* k : { &r->morphA, &r->morphB })
        if (*k > index)
            ++*k;
    r->selectedVariation = index + 1;
    return index + 1;
}

bool deleteVariation (PatcherDoc& doc, int index)
{
    auto* r = rackOf (doc);
    if (r == nullptr || index < 0 || index >= (int) r->variations.size())
        return false;
    r->variations.erase (r->variations.begin() + index);
    for (auto* k : { &r->morphA, &r->morphB, &r->selectedVariation })
        *k = *k == index ? -1 : (*k > index ? *k - 1 : *k);
    compile (doc);
    return true;
}

std::array<float, rackMacroCount> morphValues (const RackDoc& rack, float amount) noexcept
{
    std::array<float, rackMacroCount> out {};
    amount = std::clamp (amount, 0.0f, 1.0f);
    for (int m = 0; m < rackMacroCount; ++m)
    {
        out[(std::size_t) m] = rack.macros[(std::size_t) m].value;
        if (morphing (rack, m))
        {
            const auto a = rack.variations[(std::size_t) rack.morphA].values[(std::size_t) m];
            const auto b = rack.variations[(std::size_t) rack.morphB].values[(std::size_t) m];
            out[(std::size_t) m] = a + (b - a) * amount;
        }
    }
    return out;
}

void setMorph (PatcherDoc& doc, float amount)
{
    auto* r = rackOf (doc);
    if (r == nullptr)
        return;
    r->morph = std::clamp (amount, 0.0f, 1.0f);
    if ((std::size_t) morphOutput < doc.surface.size())
        doc.surface[(std::size_t) morphOutput].value = r->morph;
    if (! r->morphOn)
        return;
    const auto values = morphValues (*r, r->morph);
    for (int m = 0; m < rackMacroCount; ++m)
        if (morphing (*r, m))
        {
            // (the macro's shown value and the document follow; its cables already come from the Morph knob)
            setMacro (doc, m, values[(std::size_t) m]);
            r = rackOf (doc);
        }
}

// ---------------------------------------------------------------------------------------------------------------
// The chain mixer node (EffectKind::rackMixer)

namespace
{
class ChainMixer final : public Effect
{
public:
    void prepare (double) noexcept override {}
    void reset() noexcept override { primed = false; }
    void process (float* l, float* r, int n, const EffectParams& p, double) noexcept override
    {
        float pl = 1.0f, pr = 1.0f;
        panGains (p[(std::size_t) mixerPan] * 2.0f - 1.0f, pl, pr);
        const auto g = volumeGain (p[(std::size_t) mixerVolume]) * std::clamp (p[(std::size_t) mixerSelect], 0.0f, 1.0f);
        const auto tl = g * pl, tr = g * pr;
        if (! primed)
        {
            gl = tl;
            gr = tr;
            primed = true;
        }
        if (n <= 0)
            return;
        // a linear ramp over the block to the new gains (no zipper when a macro or the selector moves)
        const auto dl = (tl - gl) / (float) n, dr = (tr - gr) / (float) n;
        if (dl == 0.0f && dr == 0.0f)
        {
            if (gl == 1.0f && gr == 1.0f)
                return;
            for (int i = 0; i < n; ++i)
            {
                l[i] *= gl;
                r[i] *= gr;
            }
            return;
        }
        for (int i = 0; i < n; ++i)
        {
            gl += dl;
            gr += dr;
            l[i] *= gl;
            r[i] *= gr;
        }
        gl = tl;
        gr = tr;
    }

private:
    float gl = 1.0f, gr = 1.0f;
    bool primed = false;
};
} // namespace

std::unique_ptr<Effect> makeChainMixer() { return std::make_unique<ChainMixer>(); }
// ---------------------------------------------------------------------------------------------------------------
// Drum Rack (round 190ec)

PatcherDoc makeDrumRack()
{
    PatcherDoc doc;
    RackDoc r;
    r.kind = RackDoc::Kind::instrument;
    r.drum = true;
    r.showMacros = false; // (Live's empty Drum Rack: the pads and the devices pane only, 190ei B1a)
    r.showChains = false;
    r.showVariations = false;
    r.showDevices = true;
    r.showPads = true;
    r.padScroll = drumDefaultScroll;
    r.selectedChain = 0;
    doc.rack.push_back (std::move (r));
    compile (doc);
    return doc;
}

bool isDrumRack (const PatcherDoc& doc) noexcept
{
    const auto* r = rackOf (doc);
    return r != nullptr && r->drum;
}

bool isDrumRack (const ChannelDef& channel) noexcept { return isRack (channel) && isDrumRack (channel.patcher); }

int padRow (int key) noexcept { return drumPadRows - 1 - std::clamp (key, 0, drumPadCount - 1) / drumPadColumns; }
int rowFirstKey (int row) noexcept { return (drumPadRows - 1 - std::clamp (row, 0, drumPadRows - 1)) * drumPadColumns; }

int chainSlots (const RackDoc& rack) noexcept { return (int) (rack.chains.size() + rack.returns.size()); }
bool isReturnSlot (const RackDoc& rack, int slot) noexcept { return slot >= (int) rack.chains.size() && slot < chainSlots (rack); }

RackChain* chainAt (RackDoc& rack, int slot) noexcept
{
    if (slot < 0)
        return nullptr;
    if (slot < (int) rack.chains.size())
        return &rack.chains[(std::size_t) slot];
    slot -= (int) rack.chains.size();
    return slot < (int) rack.returns.size() ? &rack.returns[(std::size_t) slot] : nullptr;
}

const RackChain* chainAt (const RackDoc& rack, int slot) noexcept { return chainAt (const_cast<RackDoc&> (rack), slot); }

int padChain (const RackDoc& rack, int key) noexcept
{
    for (int c = 0; c < (int) rack.chains.size(); ++c)
        if (rack.chains[(std::size_t) c].receive == key)
            return c;
    return -1;
}

std::string padName (const RackDoc& rack, int key)
{
    const auto c = padChain (rack, key);
    return c >= 0 ? rack.chains[(std::size_t) c].name : std::string();
}

std::map<int, std::string> padNames (const RackDoc& rack)
{
    std::map<int, std::string> names;
    if (! rack.drum)
        return names;
    for (const auto& c : rack.chains)
        if (c.receive >= 0 && c.receive < drumPadCount && ! c.name.empty())
            names.emplace (c.receive, c.name); // (the first chain on a key names its pad)
    return names;
}

std::vector<int> filledPadKeys (const RackDoc& rack)
{
    std::vector<int> keys;
    if (! rack.drum)
        return keys;
    for (int key = drumPadCount - 1; key >= 0; --key)
        if (padChain (rack, key) >= 0)
            keys.push_back (key);
    return keys;
}

int copyPad (const PatcherDoc& from, int chain, PatcherDoc& to, int key, const std::string& samplePath)
{
    const auto* src = rackOf (from);
    if (src == nullptr || chain < 0 || chain >= (int) src->chains.size() || ! isDrumRack (to))
        return -1;
    const auto& c = src->chains[(std::size_t) chain];
    auto name = c.name;
    std::optional<PatchNode> instrument;
    std::vector<PatchNode> others;
    for (const auto n : c.nodes)
        if (n >= 0 && n < (int) from.nodes.size())
        {
            const auto& node = from.nodes[(std::size_t) n];
            if (! instrument && isInstrumentNode (node))
                instrument = node;
            else
                others.push_back (node);
        }
    if (! samplePath.empty())
    {
        const auto slash = samplePath.find_last_of ("/\\");
        auto stem = samplePath.substr (slash == std::string::npos ? 0 : slash + 1);
        if (const auto dot = stem.find_last_of ('.'); dot != std::string::npos && dot > 0)
            stem.resize (dot);
        if (! instrument || instrument->channel.instrument != InstrumentKind::sampler)
        {
            PatchNode n;
            n.generator = true;
            n.channel.instrument = InstrumentKind::sampler;
            n.channel.sampler.release = 0.0f; // (Live's pad Simpler: one-shot)
            instrument = n;
        }
        instrument->channel.samplePath = samplePath;
        instrument->channel.name = stem;
        name = stem;
    }
    const auto k = addPad (to, key, name, instrument);
    if (k < 0)
        return -1;
    for (const auto& node : others)
        addDevice (to, k, node);
    auto* r = rackOf (to);
    auto& dst = r->chains[(std::size_t) k];
    dst.volume = c.volume;
    dst.pan = c.pan;
    dst.active = c.active;
    dst.play = c.play;
    dst.choke = c.choke;
    compile (to);
    return k;
}

std::string returnLetter (int k) { return std::string (1, (char) ('a' + std::clamp (k, 0, 25))); }

std::string returnLabel (const RackDoc& rack, int k)
{
    if (k < 0 || k >= (int) rack.returns.size())
        return {};
    const auto& name = rack.returns[(std::size_t) k].name;
    return name.empty() ? returnLetter (k) : returnLetter (k) + " " + name;
}

int addPad (PatcherDoc& doc, int key, const std::string& name, std::optional<PatchNode> instrument)
{
    auto* r = rackOf (doc);
    if (r == nullptr || ! r->drum)
        return -1;
    RackChain c;
    c.name = name;
    c.receive = std::clamp (key, -1, drumPadCount - 1);
    c.play = drumPlayNote;
    c.sends.assign (r->returns.size(), RackSend {});
    addChainNodes (doc, c, RackDoc::Kind::instrument);
    r = rackOf (doc);
    r->chains.push_back (std::move (c));
    const auto index = (int) r->chains.size() - 1;
    r->selectedChain = index;
    if (instrument)
        addDevice (doc, index, std::move (*instrument)); // (compiles)
    else
        compile (doc);
    return index;
}

bool swapPads (PatcherDoc& doc, int fromKey, int toKey)
{
    auto* r = rackOf (doc);
    if (r == nullptr || ! r->drum || fromKey == toKey || fromKey < 0 || toKey < 0 || fromKey >= drumPadCount || toKey >= drumPadCount)
        return false;
    bool any = false;
    for (auto& c : r->chains)
        if (c.receive == fromKey)
            c.receive = toKey, any = true;
        else if (c.receive == toKey)
            c.receive = fromKey, any = true;
    if (any)
        compile (doc);
    return any;
}

namespace
{
template <typename F>
void editChain (PatcherDoc& doc, int chain, F&& f)
{
    if (auto* r = rackOf (doc); r != nullptr && chain >= 0 && chain < (int) r->chains.size())
    {
        f (*r, r->chains[(std::size_t) chain]);
        compile (doc);
    }
}
} // namespace

void setReceive (PatcherDoc& doc, int chain, int key)
{
    editChain (doc, chain, [key] (RackDoc&, RackChain& c) { c.receive = std::clamp (key, -1, drumPadCount - 1); });
}

void setPlay (PatcherDoc& doc, int chain, int key)
{
    editChain (doc, chain, [key] (RackDoc&, RackChain& c) { c.play = std::clamp (key, 0, drumPadCount - 1); });
}

void setChoke (PatcherDoc& doc, int chain, int group)
{
    editChain (doc, chain, [group] (RackDoc&, RackChain& c) { c.choke = std::clamp (group, 0, drumChokeGroups); });
}

void setAudioTo (PatcherDoc& doc, int chain, int target)
{
    editChain (doc, chain, [target] (RackDoc& r, RackChain& c) { c.audioTo = target >= 0 && target < (int) r.returns.size() ? target : -1; });
}

void setSend (PatcherDoc& doc, int chain, int returnChain, float level, bool on)
{
    editChain (doc, chain, [=] (RackDoc& r, RackChain& c) {
        if (returnChain < 0 || returnChain >= (int) r.returns.size())
            return;
        c.sends.resize (r.returns.size());
        c.sends[(std::size_t) returnChain] = { std::clamp (level, 0.0f, 1.0f), on };
    });
}

int addReturn (PatcherDoc& doc, const std::string& name)
{
    auto* r = rackOf (doc);
    if (r == nullptr || ! r->drum)
        return -1;
    RackChain ret;
    // Live shows a return chain with its letter before its name ("a Reverb", returnLabel); the name is kept without it
    // (Live's UserName [DATA]), so a new unnamed one shows its letter alone
    ret.name = name;
    ret.receive = -1;
    addChainNodes (doc, ret, RackDoc::Kind::instrument);
    r = rackOf (doc);
    r->returns.push_back (std::move (ret));
    for (auto& c : r->chains)
        c.sends.resize (r->returns.size());
    compile (doc);
    return (int) r->returns.size() - 1;
}

bool removeReturn (PatcherDoc& doc, int k)
{
    auto* r = rackOf (doc);
    if (r == nullptr || k < 0 || k >= (int) r->returns.size())
        return false;
    for (auto& c : r->chains)
    {
        if ((int) c.sends.size() > k)
            c.sends.erase (c.sends.begin() + k);
        c.audioTo = c.audioTo == k ? -1 : (c.audioTo > k ? c.audioTo - 1 : c.audioTo);
    }
    auto nodes = r->returns[(std::size_t) k].nodes;
    if (r->returns[(std::size_t) k].mixer >= 0)
        nodes.push_back (r->returns[(std::size_t) k].mixer);
    r->returns.erase (r->returns.begin() + k);
    std::sort (nodes.rbegin(), nodes.rend());
    for (const auto n : nodes)
        eraseNode (doc, n);
    r = rackOf (doc);
    r->selectedChain = std::clamp (r->selectedChain, 0, std::max (0, chainSlots (*r) - 1));
    compile (doc);
    return true;
}

ChannelDef sliceToDrumRack (const std::string& samplePath, const std::vector<float>& slices, const std::string& baseName, SliceTemplate preset, float end,
                            const std::string& rackName)
{
    ChannelDef channel;
    channel.instrument = InstrumentKind::patcher;
    channel.name = rackName.empty() ? baseName : rackName;
    channel.patcher = makeDrumRack();
    rackOf (channel.patcher)->name = rackName;
    std::vector<float> starts;
    for (const auto x : slices)
        if (x >= 0.0f && x < end && (starts.empty() || x > starts.back()))
            starts.push_back (x);
    if (starts.empty())
        starts.push_back (0.0f);
    const auto count = std::min ((int) starts.size(), maxSlicePads);
    for (int i = 0; i < count; ++i)
    {
        PatchNode n;
        n.generator = true;
        auto& s = n.channel;
        s.instrument = InstrumentKind::sampler;
        s.name = baseName + " " + std::to_string (i + 1);
        s.samplePath = samplePath;
        s.sampler.regionStart = std::clamp (starts[(std::size_t) i], 0.0f, 1.0f);
        s.sampler.regionEnd = std::clamp (i + 1 < (int) starts.size() ? starts[(std::size_t) i + 1] : end, s.sampler.regionStart, 1.0f);
        // Live's Default slicing: Classic mode with a 50 ms release, one voice, every pad in choke group 1; the one-shot
        // template plays each slice to its end (LYNX's Sampler: release 0 = one-shot)
        s.sampler.release = preset == SliceTemplate::gated ? 0.05f : 0.0f;
        s.sampler.cut = preset == SliceTemplate::gated; // (one voice: a new note on the pad cuts the last)
        const auto name = s.name; // (copied first: the pad's node is moved into addPad)
        const auto chain = addPad (channel.patcher, drumFirstPad + i, name, std::move (n));
        if (preset == SliceTemplate::gated)
            rackOf (channel.patcher)->chains[(std::size_t) chain].choke = 1;
    }
    auto* r = rackOf (channel.patcher);
    r->selectedChain = 0;
    r->padScroll = drumDefaultScroll;
    compile (channel.patcher);
    return channel;
}
// ---------------------------------------------------------------------------------------------------------------
// Slicing helpers (round 190ec)

std::vector<float> slicesInRegion (const std::vector<float>& starts, float from, float to)
{
    from = std::clamp (from, 0.0f, 1.0f);
    to = std::clamp (to, from, 1.0f);
    std::vector<float> sorted;
    for (const auto x : starts)
        if (x > from && x < to)
            sorted.push_back (x);
    std::sort (sorted.begin(), sorted.end());
    std::vector<float> out { from };
    for (const auto x : sorted)
        if (x - out.back() > 1.0e-6f)
            out.push_back (x);
    return out;
}

std::vector<float> gridSlices (double fileSeconds, float from, float to, double secondsPerSlice)
{
    from = std::clamp (from, 0.0f, 1.0f);
    to = std::clamp (to, from, 1.0f);
    std::vector<float> out { from };
    if (! (fileSeconds > 0.0) || ! (secondsPerSlice > 0.0))
        return out;
    const auto step = secondsPerSlice / fileSeconds;
    const auto count = (int) std::ceil (((double) to - (double) from) / step - 1.0e-6);
    for (int i = 1; i < count && (int) out.size() < drumPadCount; ++i)
        out.push_back ((float) ((double) from + step * i));
    return out;
}

std::vector<Note> sliceNotes (const std::vector<float>& starts, float end, const std::function<double (float)>& tickOf, int channel)
{
    std::vector<Note> notes;
    const auto count = std::min ((int) starts.size(), maxSlicePads);
    for (int i = 0; i < count; ++i)
    {
        const auto a = tickOf (starts[(std::size_t) i]);
        const auto b = tickOf (i + 1 < (int) starts.size() ? starts[(std::size_t) i + 1] : end);
        if (b <= 0.0 || b <= a)
            continue;
        Note n;
        n.start = (std::int64_t) std::llround (std::max (0.0, a));
        n.length = std::max<std::int64_t> (1, (std::int64_t) std::llround (b) - n.start);
        n.key = drumFirstPad + i;
        n.channel = channel;
        notes.push_back (n);
    }
    return notes;
}

int addSliceTrack (Song& song, int clipIndex, ChannelDef drumRack, std::vector<Note> notes, const std::string& patternName)
{
    if (clipIndex < 0 || clipIndex >= (int) song.playlist.size() || notes.empty())
        return -1;
    const auto source = song.playlist[(std::size_t) clipIndex];
    if (source.kind != ClipKind::audio)
        return -1;
    const auto channel = (int) song.channels.size();
    drumRack.mixerTrack = song.claimFreeInsert();
    song.channels.push_back (std::move (drumRack));
    Pattern pattern;
    pattern.name = patternName;
    for (auto& n : notes)
        n.channel = channel;
    pattern.notes = std::move (notes);
    pattern.sortNotes();
    song.patterns.push_back (std::move (pattern));
    const auto track = source.track + 1;
    noteedit::insertTrack (song, track);
    if (track < (int) song.playlistTracks.size())
        song.playlistTracks[(std::size_t) track].name = patternName; // (Live: the new MIDI track is named after the clip)
    Clip clip;
    clip.kind = ClipKind::pattern;
    clip.pattern = (int) song.patterns.size() - 1;
    clip.track = track;
    clip.start = source.start;
    clip.length = source.length;
    song.playlist.push_back (clip);
    return channel;
}

const char* slicePerName (SlicePer per) noexcept
{
    switch (per)
    {
        case SlicePer::warpMarker: return "Warp Marker";
        case SlicePer::transient: return "Transient";
        case SlicePer::bar: return "Bar";
        case SlicePer::half: return "1/2 Note";
        case SlicePer::quarter: return "1/4 Note";
        case SlicePer::eighth: return "1/8 Note";
        case SlicePer::sixteenth: return "1/16 Note";
        case SlicePer::thirtySecond: return "1/32 Note";
    }
    return "";
}

double slicePerBeats (SlicePer per, int beatsPerBar) noexcept
{
    switch (per)
    {
        case SlicePer::bar: return (double) std::max (1, beatsPerBar);
        case SlicePer::half: return 2.0;
        case SlicePer::quarter: return 1.0;
        case SlicePer::eighth: return 0.5;
        case SlicePer::sixteenth: return 0.25;
        case SlicePer::thirtySecond: return 0.125;
        case SlicePer::warpMarker:
        case SlicePer::transient: break;
    }
    return 0.0;
}
} // namespace lynx::rack
