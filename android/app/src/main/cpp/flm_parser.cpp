#include "flm_parser.h"

#include <algorithm>
#include <cstring>

namespace lynx::flm_android
{

// ---------- Helper: Read remaining body as bytes ----------

static std::vector<std::uint8_t> readRemaining(ChunkReader& r, std::uint32_t size)
{
    std::vector<std::uint8_t> buf(size);
    if (r.pos() + size > r.size()) return {};
    std::memcpy(buf.data(), r.data_.data() + r.pos_, size);
    r.pos_ += size;
    return buf;
}

// ---------- Parse SampleRef ----------

static void parseSampleRef(ChunkReader& r, SampleRef& ref)
{
    while (!r.eof())
    {
        ChunkHeader h;
        if (!r.readHeader(h)) break;
        if (r.pos() + h.size > r.size()) break;
        
        switch (h.tag)
        {
            case tag::MAIN: ref.name = r.readString(); break;
            case tag::PTH1: ref.root = r.readString(); break;
            case tag::PTH1: ref.path = r.readString(); break; // PTH1 has two strings
            case tag::PRST: ref.savedPath = r.readString(); break;
            case tag::PRST: ref.relName = r.readString(); break; // PRST has two strings
            case tag::PRMS:
            {
                std::uint32_t count = h.size / 4;
                ref.params.resize(count);
                for (std::uint32_t i = 0; i < count; ++i)
                    ref.params[i] = r.readF32();
                break;
            }
            case tag::STR1:
            {
                ref.stretchMode = static_cast<int>(r.readU32());
                ref.stretch = r.readF64();
                ref.pitch = r.readF64();
                break;
            }
            default: r.pos_ += h.size; break;
        }
        if (r.pos() >= r.size()) break;
    }
}

// ---------- Parse Event (EVT2) ----------

static void parseEvent(ChunkReader& r, Event& evt)
{
    evt.tick = r.readU32();
    evt.length = r.readF64();
    evt.key = static_cast<int>(r.readU32());
    evt.velocity = r.readU8();
    evt.pan = r.readU8();
    evt.value = r.readU16();
    evt.repeat = static_cast<std::int8_t>(r.readU8());
    evt.slide = r.readU8();
}

// ---------- Parse Clip ----------

static void parseClip(ChunkReader& r, Clip& clip)
{
    while (!r.eof())
    {
        ChunkHeader h;
        if (!r.readHeader(h)) break;
        if (r.pos() + h.size > r.size()) break;
        
        switch (h.tag)
        {
            case tag::CLHd:
            {
                clip.startTick = r.readU32();
                clip.length = r.readF64();
                clip.loopLength = r.readF64();
                clip.offset = r.readF64();
                clip.contentId = r.readU32();
                clip.sourceLength = r.readF64();
                clip.flag = r.readU8();
                break;
            }
            case tag::EVT2:
            {
                Event evt;
                parseEvent(r, evt);
                clip.events.push_back(evt);
                break;
            }
            case tag::CLSm:
            {
                SampleRef sample;
                parseSampleRef(r, sample);
                clip.sample = sample;
                break;
            }
            default: r.pos_ += h.size; break;
        }
        if (r.pos() >= r.size()) break;
    }
}

// ---------- Parse Lane ----------

static void parseLane(ChunkReader& r, Lane& lane)
{
    while (!r.eof())
    {
        ChunkHeader h;
        if (!r.readHeader(h)) break;
        if (r.pos() + h.size > r.size()) break;
        
        switch (h.tag)
        {
            case tag::LNHD:
            {
                lane.rawKind = r.readU32();
                lane.targetModule = static_cast<int>(r.readU32());
                lane.targetParam = static_cast<int>(r.readU32());
                lane.name = r.readString();
                break;
            }
            case tag::DESc:
            {
                lane.swing = r.readF64();
                break;
            }
            case tag::CLIP:
            {
                Clip clip;
                parseClip(r, clip);
                lane.clips.push_back(std::move(clip));
                break;
            }
            default: r.pos_ += h.size; break;
        }
        if (r.pos() >= r.size()) break;
    }
}

// ---------- Parse Channel ----------

static void parseChannel(ChunkReader& r, Channel& ch)
{
    while (!r.eof())
    {
        ChunkHeader h;
        if (!r.readHeader(h)) break;
        if (r.pos() + h.size > r.size()) break;
        
        switch (h.tag)
        {
            case tag::CHHD:
            {
                ch.kind = r.readU32();
                ch.name = r.readString();
                ch.typeName = r.readString();
                ch.userName = r.readString();
                ch.order = r.readF64();
                ch.hue = r.readF32();
                ch.channelId = static_cast<int>(r.readU32());
                break;
            }
            case tag::SEND:
            {
                std::uint32_t count = h.size / 4;
                std::vector<std::uint32_t> sendData(count);
                for (std::uint32_t i = 0; i < count; ++i)
                    sendData[i] = r.readU32();
                ch.sends.push_back(std::move(sendData));
                break;
            }
            case tag::LANE:
            {
                Lane lane;
                parseLane(r, lane);
                ch.lanes.push_back(std::move(lane));
                break;
            }
            default: r.pos_ += h.size; break;
        }
        if (r.pos() >= r.size()) break;
    }
}

// ---------- Parse Module ----------

static void parseModule(ChunkReader& r, Module& mod)
{
    while (!r.eof())
    {
        ChunkHeader h;
        if (!r.readHeader(h)) break;
        if (r.pos() + h.size > r.size()) break;
        
        switch (h.tag)
        {
            case tag::MODH:
            {
                mod.type = r.readU32();
                mod.instanceId = r.readU32();
                mod.bypassFlag = r.readU8() != 0;
                mod.keyLow = static_cast<int>(r.readU32());
                mod.keyHigh = static_cast<int>(r.readU32());
                mod.descName = r.readString();
                mod.descFolder = r.readString();
                mod.presetFile = r.readString();
                mod.presetName = r.readString();
                break;
            }
            case tag::MODP:
            {
                std::uint32_t count = h.size / 4;
                mod.params.resize(count);
                for (std::uint32_t i = 0; i < count; ++i)
                    mod.params[i] = r.readF32();
                break;
            }
            case tag::MODP: // SMPR (reserved)
            {
                std::uint32_t count = h.size / 4;
                mod.reserved.resize(count);
                for (std::uint32_t i = 0; i < count; ++i)
                    mod.reserved[i] = r.readF32();
                break;
            }
            case tag::CSTM:
            {
                mod.custom = readRemaining(r, h.size);
                break;
            }
            default: r.pos_ += h.size; break;
        }
        if (r.pos() >= r.size()) break;
    }
}

// ---------- Parse Rack ----------

static void parseRack(ChunkReader& r, Rack& rack)
{
    while (!r.eof())
    {
        ChunkHeader h;
        if (!r.readHeader(h)) break;
        if (r.pos() + h.size > r.size()) break;
        
        switch (h.tag)
        {
            case tag::RPRM:
            {
                std::uint32_t count = h.size / 4;
                rack.params.resize(count);
                for (std::uint32_t i = 0; i < count; ++i)
                    rack.params[i] = r.readF32();
                break;
            }
            case tag::RSMP:
            {
                // RSMP has PADS + kit samples
                // First 4 bytes = pads count, then kit samples
                std::uint32_t padCount = r.readU32();
                rack.pads.resize(padCount);
                for (std::uint32_t i = 0; i < padCount; ++i)
                    rack.pads[i] = r.readU8();
                
                // Kit samples
                while (!r.eof() && r.pos() < r.size())
                {
                    SampleRef sample;
                    parseSampleRef(r, sample);
                    rack.kit.push_back(std::move(sample));
                    if (r.pos() >= r.size()) break;
                }
                break;
            }
            case tag::MODL:
            {
                Module mod;
                parseModule(r, mod);
                rack.modules.push_back(std::move(mod));
                break;
            }
            case tag::CCLK:
            {
                std::uint32_t count = h.size / 12; // 3 x u32
                for (std::uint32_t i = 0; i < count; ++i)
                {
                    Rack::CcLink link;
                    link.cc = static_cast<int>(r.readU32());
                    link.module = static_cast<int>(r.readU32());
                    link.param = static_cast<int>(r.readU32());
                    rack.ccLinks.push_back(link);
                }
                break;
            }
            default: r.pos_ += h.size; break;
        }
        if (r.pos() >= r.size()) break;
    }
}

// ---------- Parse Project HEAD ----------

static void parseHead(ChunkReader& r, Project& proj)
{
    while (!r.eof())
    {
        ChunkHeader h;
        if (!r.readHeader(h)) break;
        if (r.pos() + h.size > r.size()) break;
        
        switch (h.tag)
        {
            case 0x44454148: // HEAD (already read)
                break;
            case 0x5049544E: // NITP - tempo, meter
            {
                proj.tempo = r.readF64();
                proj.numerator = static_cast<int>(r.readU32());
                proj.denominator = static_cast<int>(r.readU32());
                break;
            }
            case 0x4545534B: // KSE - key/scale
            {
                proj.root = r.readU32();
                proj.scale = r.readU32();
                break;
            }
            case tag::META:
            {
                proj.appVersion = r.readString();
                proj.created = r.readString();
                proj.saved = r.readString();
                proj.author = r.readString();
                proj.originalName = r.readString();
                break;
            }
            case tag::KEYB:
            {
                proj.chordSetJson = r.readString();
                break;
            }
            default: r.pos_ += h.size; break;
        }
        if (r.pos() >= r.size()) break;
    }
}

// ---------- Main Parse Function ----------

ParseResult parseFlm(std::span<const std::uint8_t> bytes)
{
    ParseResult result;
    
    if (bytes.size() < 8)
    {
        result.error = "File too small";
        return result;
    }
    
    // Check magic "10LF" (little-endian: 0x464C3031)
    if (bytes[0] != 0x31 || bytes[1] != 0x30 || bytes[2] != 0x4C || bytes[3] != 0x46)
    {
        result.error = "Not an FLM file (invalid magic)";
        return result;
    }
    
    ChunkReader r(bytes.subspan(4)); // Skip "10LF"
    
    // Read first chunk - should be HEAD
    ChunkHeader h;
    if (!r.readHeader(h))
    {
        result.error = "No HEAD chunk";
        return result;
    }
    
    if (h.tag != 0x44454148) // "HEAD"
    {
        result.error = "First chunk is not HEAD";
        return result;
    }
    
    // Parse HEAD
    parseHead(r, result.project);
    
    // Parse remaining top-level chunks
    while (!r.eof())
    {
        ChunkHeader h2;
        if (!r.readHeader(h2)) break;
        if (r.pos() + h2.size > r.size()) break;
        
        switch (h2.tag)
        {
            case tag::RACK:
            {
                Rack rack;
                parseRack(r, rack);
                result.project.racks.push_back(std::move(rack));
                break;
            }
            case tag::CHAN:
            {
                Channel ch;
                parseChannel(r, ch);
                result.project.channels.push_back(std::move(ch));
                break;
            }
            default:
                result.project.unknownChunks++;
                r.pos_ += h2.size;
                break;
        }
        if (r.pos() >= r.size()) break;
    }
    
    result.ok = true;
    return result;
}

} // namespace lynx::flm_android