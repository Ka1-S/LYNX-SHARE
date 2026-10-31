#include "core/MidiOut.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>

#include "core/Instruments.h"

namespace lynx::midiout
{
namespace
{
// --- FL's byte layout ---------------------------------------------------------------------------------------------

struct Reader
{
    const std::vector<std::uint8_t>& d;
    std::size_t pos = 0;
    bool ok = true;

    std::int32_t i32()
    {
        std::int32_t v = 0;
        if (pos + 4 > d.size())
        {
            ok = false;
            return 0;
        }
        std::memcpy (&v, d.data() + pos, 4);
        pos += 4;
        return v;
    }
    int u8()
    {
        if (pos >= d.size())
        {
            ok = false;
            return 0;
        }
        return d[pos++];
    }
    // FL's string: a length byte (0xFF: a 32-bit length follows), then the UTF-8 bytes; trailing control characters are
    // dropped on reading (FL does the same)
    std::string str()
    {
        std::size_t n = (std::size_t) u8();
        if (n == 0xff)
            n = (std::size_t) (std::uint32_t) i32();
        if (! ok || pos + n > d.size() || n > 1u << 20)
        {
            ok = false;
            return {};
        }
        std::string s (reinterpret_cast<const char*> (d.data() + pos), n);
        pos += n;
        while (! s.empty() && (unsigned char) s.back() < 0x20)
            s.pop_back();
        return s;
    }
};

void putI32 (std::vector<std::uint8_t>& d, std::int32_t v)
{
    std::uint8_t b[4];
    std::memcpy (b, &v, 4);
    d.insert (d.end(), b, b + 4);
}

void putStr (std::vector<std::uint8_t>& d, const std::string& s)
{
    if (s.size() < 0xff)
        d.push_back ((std::uint8_t) s.size());
    else
    {
        d.push_back (0xff);
        putI32 (d, (std::int32_t) s.size());
    }
    d.insert (d.end(), s.begin(), s.end());
}

int clampValue (const Control& c, int v) noexcept
{
    // FL: the value is raised to min, then lowered to max
    v = std::max (v, c.min);
    return std::min (v, c.max);
}
} // namespace

State::State()
{
    for (int p = 0; p < pageCount; ++p)
        pageNames[(std::size_t) p] = "Page " + std::to_string (p + 1);
}

std::optional<State> parse (const std::vector<std::uint8_t>& data)
{
    Reader r { data };
    State s;
    s.version = r.i32();
    if (! r.ok || s.version < 0 || s.version > 64)
        return std::nullopt;
    // the header block: 20 bytes before version 2, 24 before version 4, then 26 (the two option bytes)
    const auto v = s.version;
    const auto headerBytes = (std::size_t) ((6 - (v < 2 ? 1 : 0)) * 4 + (v > 3 ? 2 : 0));
    if (r.pos + headerBytes + 4 * (1 + controlCount) > data.size())
        return std::nullopt;
    // (a state that does not hold the option bytes keeps "Send pan, volume and pitch" on, as FL loads it)
    s.sendPanVolPitch = true;
    s.mapNoteColour = false;
    s.channel = r.i32();
    s.reserved = r.i32();
    s.bankMsb = r.i32();
    s.bankLsb = r.i32();
    s.page = r.i32();
    if (headerBytes >= 24)
        s.port = r.i32();
    if (headerBytes >= 26)
    {
        s.sendPanVolPitch = r.u8() != 0;
        s.mapNoteColour = r.u8() != 0;
    }
    s.program = r.i32();
    for (auto& value : s.values)
        value = r.i32();
    if (! r.ok)
        return std::nullopt;
    s.channel = std::clamp (s.channel, 0, 15);
    s.port = std::clamp (s.port, 0, 255);
    s.page = std::clamp (s.page, 0, pageCount - 1);
    s.program = std::clamp (s.program, 0, 128);
    s.bankMsb = s.bankMsb < 0 ? -1 : std::min (s.bankMsb, 127);
    s.bankLsb = s.bankLsb < 0 ? -1 : std::min (s.bankLsb, 127);
    // before version 6 the reset options follow "Send pan, volume and pitch" (cut notes always on)
    if (v <= 5)
        s.resetFlags = (s.sendPanVolPitch ? resetPitch | resetBendRange | resetPan | resetVolume | resetProgram : 0) | resetCutNotes;

    // the controls that differ from the default: index, then 16 (version < 3), 20 (< 7) or 24 bytes, then two strings
    const auto recordWords = v < 3 ? 4 : v < 7 ? 5 : 6;
    for (int guard = 0; guard <= controlCount; ++guard)
    {
        const auto index = r.u8();
        if (! r.ok)
            return s; // (an old state may end here)
        if (index == 0xff)
            break;
        std::int32_t w[6] { -1, 0, 0, 127, -1, 0 };
        for (int k = 0; k < recordWords; ++k)
            w[k] = r.i32();
        if (recordWords < 6)
            w[5] = w[2]; // (FL: before version 7 the note-off value is the minimum)
        Control c;
        c.number = w[0] == -1 ? -1 : (int) (w[0] & 0x7fffffff);
        c.active = w[0] != -1 && w[0] >= 0;
        c.type = (Type) std::clamp ((int) w[1], 0, 4);
        c.min = w[2];
        c.max = w[3];
        c.channel = w[4] < 0 ? -1 : std::min ((int) w[4], 15);
        c.noteOff = w[5];
        c.name = r.str();
        c.shortName = r.str();
        if (! r.ok)
            return std::nullopt;
        if (index < controlCount)
            s.controls[(std::size_t) index] = std::move (c);
    }
    for (auto& name : s.pageNames)
    {
        auto n = r.str();
        if (! r.ok)
            return s;
        name = std::move (n);
    }
    if (v > 5)
    {
        const auto off = r.i32();
        if (r.ok)
            s.resetFlags = ~off & resetAll; // (FL writes the options inverted: a set bit = the option is off)
    }
    return s;
}

std::vector<std::uint8_t> write (const State& s)
{
    std::vector<std::uint8_t> d;
    d.reserve (400);
    putI32 (d, 7);
    putI32 (d, std::clamp (s.channel, 0, 15));
    putI32 (d, s.reserved);
    putI32 (d, s.bankMsb);
    putI32 (d, s.bankLsb);
    putI32 (d, std::clamp (s.page, 0, pageCount - 1));
    putI32 (d, std::clamp (s.port, 0, 255));
    d.push_back (s.sendPanVolPitch ? 1 : 0);
    d.push_back (s.mapNoteColour ? 1 : 0);
    putI32 (d, std::clamp (s.program, 0, 128));
    for (const auto value : s.values)
        putI32 (d, value);
    for (int i = 0; i < controlCount; ++i)
    {
        const auto& c = s.controls[(std::size_t) i];
        if (! c.assigned() && c.name.empty() && c.shortName.empty())
            continue;
        d.push_back ((std::uint8_t) i);
        putI32 (d, ! c.assigned() ? -1 : c.active ? (c.number & 0x7fffffff) : (std::int32_t) ((std::uint32_t) c.number | 0x80000000u));
        putI32 (d, (int) c.type);
        putI32 (d, c.min);
        putI32 (d, c.max);
        putI32 (d, c.channel);
        putI32 (d, c.noteOff);
        putStr (d, c.name);
        putStr (d, c.shortName);
    }
    d.push_back (0xff);
    for (const auto& name : s.pageNames)
        putStr (d, name);
    putI32 (d, ~s.resetFlags & resetAll);
    return d;
}

State stateOf (const std::vector<std::uint8_t>& state)
{
    if (auto s = parse (state))
        return *s;
    return {};
}

std::vector<std::uint8_t> stateFromFst (const std::vector<std::uint8_t>& b)
{
    if (b.size() < 22 || std::memcmp (b.data(), "FLhd", 4) != 0)
        return {};
    std::int32_t headerSize = 0;
    std::memcpy (&headerSize, b.data() + 4, 4);
    auto pos = 8 + (std::size_t) std::max (0, headerSize);
    if (pos + 8 > b.size() || std::memcmp (b.data() + pos, "FLdt", 4) != 0)
        return {};
    pos += 8;
    while (pos < b.size())
    {
        const auto id = b[pos++];
        std::size_t len = 0;
        if (id < 64)
            len = 1;
        else if (id < 128)
            len = 2;
        else if (id < 192)
            len = 4;
        else
        {
            int shift = 0;
            while (pos < b.size())
            {
                const auto c = b[pos++];
                len |= (std::size_t) (c & 0x7f) << shift;
                shift += 7;
                if (! (c & 0x80) || shift > 28)
                    break;
            }
        }
        if (pos + len > b.size())
            return {};
        if (id == 0xD5)
            return { b.begin() + (std::ptrdiff_t) pos, b.begin() + (std::ptrdiff_t) (pos + len) };
        pos += len;
    }
    return {};
}

std::vector<std::uint8_t> writeFst (const State& state)
{
    // FL's own preset layout (its MIDI Out .fst files): header FLhd (format 0x30, 2, 96 PPQ), then FLdt with the
    // version text (0xC7), the plugin name (0xC9) and the state (0xD5)
    std::vector<std::uint8_t> events;
    auto text = [&] (std::uint8_t id, const std::vector<std::uint8_t>& data) {
        events.push_back (id);
        auto n = data.size();
        do
        {
            auto c = (std::uint8_t) (n & 0x7f);
            n >>= 7;
            if (n != 0)
                c |= 0x80;
            events.push_back (c);
        } while (n != 0);
        events.insert (events.end(), data.begin(), data.end());
    };
    const char version[] = "2.9.0", name[] = "MIDI out";
    text (0xC7, { version, version + sizeof (version) });
    text (0xC9, { name, name + sizeof (name) });
    text (0xD5, write (state));
    std::vector<std::uint8_t> out { 'F', 'L', 'h', 'd', 6, 0, 0, 0, 0x30, 0, 2, 0, 96, 0, 'F', 'L', 'd', 't' };
    const auto n = (std::uint32_t) events.size();
    for (int i = 0; i < 4; ++i)
        out.push_back ((std::uint8_t) (n >> (8 * i)));
    out.insert (out.end(), events.begin(), events.end());
    return out;
}

int valueFromNormal (const Control& c, float normal) noexcept
{
    if (! std::isfinite (normal))
        normal = 0.0f;
    normal = std::clamp (normal, 0.0f, 1.0f);
    return c.min + (int) std::lround ((double) normal * (double) (c.max - c.min));
}

float normalFromValue (const Control& c, int value) noexcept
{
    if (c.max == c.min)
        return 0.0f;
    return std::clamp ((float) ((double) (value - c.min) / (double) (c.max - c.min)), 0.0f, 1.0f);
}

int programFromNormal (float normal) noexcept
{
    return (int) std::lround ((double) std::clamp (std::isfinite (normal) ? normal : 0.0f, 0.0f, 1.0f) * 128.0);
}

float normalFromProgram (int program) noexcept
{
    return (float) std::clamp (program, 0, 128) / 128.0f;
}

float paramValue (const State& s, int k) noexcept
{
    if (k == programParam)
        return normalFromProgram (s.program);
    if (k < 1 || k > controlCount)
        return 0.0f;
    return normalFromValue (s.controls[(std::size_t) (k - 1)], s.values[(std::size_t) (k - 1)]);
}

bool setParamValue (State& s, int k, float normal) noexcept
{
    if (k == programParam)
    {
        s.program = programFromNormal (normal);
        return true;
    }
    if (k < 1 || k > controlCount)
        return false;
    auto& c = s.controls[(std::size_t) (k - 1)];
    s.values[(std::size_t) (k - 1)] = valueFromNormal (c, normal);
    if (c.assigned())
        c.active = true;
    return true;
}

std::string paramName (const State& s, int k)
{
    if (k == programParam)
        return "Patch";
    if (k < 1 || k > controlCount)
        return {};
    const auto l = label (s.controls[(std::size_t) (k - 1)]);
    return l.empty() ? "Control " + std::to_string (k) : l;
}

const char* typeName (Type t) noexcept
{
    switch (t)
    {
        case Type::cc: return "CC";
        case Type::rpn: return "RPN";
        case Type::nrpn: return "NRPN";
        case Type::aftertouch: return "Channel aftertouch";
        case Type::note: return "Note";
    }
    return "CC";
}

std::string label (const Control& c)
{
    if (! c.assigned())
        return {};
    if (! c.shortName.empty())
        return c.shortName;
    if (! c.name.empty())
        return c.name;
    if (c.type == Type::aftertouch)
        return "Aftertouch";
    return std::string (typeName (c.type)) + " " + std::to_string (c.number);
}

std::string description (const Control& c)
{
    if (! c.assigned())
        return {};
    std::string s = c.name;
    auto add = [&s] (const std::string& part) {
        if (! s.empty())
            s += ' ';
        s += part;
    };
    if (c.type == Type::aftertouch)
        add (typeName (c.type));
    else
        add (std::string (typeName (c.type)) + " " + std::to_string (c.number));
    if (c.channel >= 0)
        add ("Chan #" + std::to_string (c.channel + 1));
    return s;
}

int controlMessages (const State& s, int i, int value, int& held, Message* out) noexcept
{
    if (i < 0 || i >= controlCount)
        return 0;
    const auto& c = s.controls[(std::size_t) i];
    if (! c.sends())
        return 0;
    const auto ch = (c.channel >= 0 ? c.channel : s.channel) & 15;
    const auto v = clampValue (c, value);
    const auto wide = c.max >= 128;
    int n = 0;
    switch (c.type)
    {
        case Type::cc:
            if (! wide)
                out[n++] = pack (0xB0 | ch, c.number, v);
            else
            {
                out[n++] = pack (0xB0 | ch, c.number, v >> 7);
                out[n++] = pack (0xB0 | ch, c.number + 32, v & 127);
            }
            break;
        case Type::rpn:
        case Type::nrpn:
        {
            const auto rpn = c.type == Type::rpn;
            out[n++] = pack (0xB0 | ch, rpn ? 101 : 99, c.number >> 7);
            out[n++] = pack (0xB0 | ch, rpn ? 100 : 98, c.number & 127);
            if (! wide)
                out[n++] = pack (0xB0 | ch, 6, v);
            else
            {
                out[n++] = pack (0xB0 | ch, 6, v >> 7);
                out[n++] = pack (0xB0 | ch, 38, v & 127);
            }
            break;
        }
        case Type::aftertouch:
            out[n++] = pack (0xD0 | ch, v);
            break;
        case Type::note:
            // the value picks the key, the number is the velocity; the note-off value only ends the held note
            if (held >= 0 && (held != v || v == c.noteOff))
            {
                out[n++] = pack (0x80 | ch, held, c.number);
                held = -1;
            }
            if (held != v && v != c.noteOff && v >= 0 && v < 128)
            {
                held = v;
                out[n++] = pack (0x90 | ch, v, c.number);
            }
            break;
    }
    return n;
}

int programMessages (const State& s, Message* out) noexcept
{
    if (s.program <= 0 || (s.resetFlags & resetProgram) == 0)
        return 0;
    out[0] = pack (0xC0 | (s.channel & 15), s.program - 1);
    return 1;
}

Message resetAllControllers (const State& s) noexcept
{
    return pack (0xB0 | (s.channel & 15), 121, 0);
}

int resetMessages (const State& s, int pan, int volume, int bend, std::array<int, controlCount>& held, Message* out) noexcept
{
    const auto ch = s.channel & 15;
    int n = 0;
    if ((s.resetFlags & resetBendRange) != 0) // RPN 0 (pitch bend range) = 12 semitones
    {
        out[n++] = pack (0xB0 | ch, 101, 0);
        out[n++] = pack (0xB0 | ch, 100, 0);
        out[n++] = pack (0xB0 | ch, 6, 12);
    }
    if (s.bankMsb >= 0)
        out[n++] = pack (0xB0 | ch, 0, s.bankMsb);
    if (s.bankLsb >= 0)
        out[n++] = pack (0xB0 | ch, 32, s.bankLsb);
    if (pan > 0 && (s.resetFlags & resetPan) != 0)
        out[n++] = pack (0xB0 | ch, 10, pan);
    if (volume > 0 && (s.resetFlags & resetVolume) != 0)
        out[n++] = pack (0xB0 | ch, 7, volume);
    if (bend > 0 && (s.resetFlags & resetPitch) != 0)
        out[n++] = pack (0xE0 | ch, bend & 127, (bend >> 7) & 127);
    n += programMessages (s, out + n);
    for (int i = 0; i < controlCount; ++i)
        n += controlMessages (s, i, s.values[(std::size_t) i], held[(std::size_t) i], out + n);
    return n;
}

int panValue (float pan) noexcept
{
    return std::clamp ((int) std::lround (((double) std::clamp (pan, -1.0f, 1.0f) + 1.0) * 64.0), 0, 127);
}

int volumeValue (float volume) noexcept
{
    return std::clamp ((int) std::lround ((double) std::clamp (volume, 0.0f, 1.0f) * 128.0), 0, 127);
}

int bendValue (float cents) noexcept
{
    // FL: 8192 + cents · 8192 / 1200, held to 0…16383
    return std::clamp (8192 + (int) std::lround ((double) cents * 8192.0 / 1200.0), 0, 16383);
}

State importTrackState()
{
    State s;
    s.version = 7;
    struct Def
    {
        int index, cc;
        const char *name, *shortName;
    };
    static constexpr Def defs[] = {
        { 0, 1, "Modulation wheel", "Mod" },   { 1, 2, "Breath control", "Breath" },     { 2, 4, "Foot controller", "Foot" },
        { 3, 5, "Portamento time", "Port. Time" }, { 4, 91, "Reverb send level", "Reverb" }, { 5, 93, "Chorus send level", "Chorus" },
        { 9, 64, "Damper pedal", "Damper" },   { 10, 65, "Portamento", "Porta." },        { 11, 66, "Sustenuto", "Sustenuto" },
        { 12, 67, "Soft pedal", "Soft pedal" }, { 13, 68, "Legato Footswitch", "Legato" }, { 14, 69, "Hold 2", "Hold 2" },
    };
    for (const auto& d : defs)
    {
        auto& c = s.controls[(std::size_t) d.index];
        c.number = d.cc;
        c.active = false;
        c.name = d.name;
        c.shortName = d.shortName;
    }
    s.program = 1; // (FL's file selects patch 1)
    s.pageNames[0] = "Basic controllers";
    s.pageNames[1] = "Boolean controllers";
    return s;
}

int importParamForCc (int cc) noexcept
{
    switch (cc)
    {
        case 1: return 1;
        case 2: return 2;
        case 4: return 3;
        case 5: return 4;
        case 91: return 5;
        case 93: return 6;
        case 64: case 65: case 66: case 67: case 68: case 69: return 10 + (cc - 64);
        default: return -1;
    }
}

// --- the audio-thread instrument --------------------------------------------------------------------------------

namespace
{
// What the audio thread reads of the state, handed over by pointer: the names are left out, so copying it on the audio
// thread never allocates (empty strings).
struct Snapshot
{
    State state;
};

class MidiOutDevice final : public Instrument
{
public:
    MidiOutDevice()
    {
        for (auto& w : wanted)
            w.store (unset, std::memory_order_relaxed);
        sent.fill (unset);
        held.fill (-1);
        forced.fill (false);
        songValue.fill (unset);
    }

    void prepare (double) noexcept override {}
    void noteOn (int, float) noexcept override {}
    void noteOff (int) noexcept override {}
    void allNotesOff() noexcept override {}
    void render (float*, float*, int, float, float) noexcept override {}
    bool idle() const noexcept override { return true; }

    // (the build thread; the audio thread may run meanwhile: it only reads what is published atomically)
    void configure (const ChannelDef& channel, double) noexcept override
    {
        State s;
        try
        {
            s = stateOf (channel.pluginState);
        }
        catch (...)
        {
            return;
        }
        const auto first = ! configured;
        configured = true;
        if (first || ! (s == lastState))
        {
            // a knob the song moved (the editor, a link, "Init song with this position"): its value goes out
            for (int i = 0; i < controlCount; ++i)
                if (first || s.values[(std::size_t) i] != songValue[(std::size_t) i])
                {
                    songValue[(std::size_t) i] = s.values[(std::size_t) i];
                    if (! first)
                        wanted[(std::size_t) i].store (s.values[(std::size_t) i], std::memory_order_relaxed);
                }
            const auto programChanged = ! first && s.program != lastState.program;
            lastState = s;
            publish (s);
            if (programChanged)
                programPending.store (true, std::memory_order_relaxed);
            dirty.store (true, std::memory_order_release);
        }
        // "Send pan, volume and pitch": the channel's pan and volume as CC 10 / CC 7 when they change
        const auto pan = panValue (channel.pan), volume = volumeValue (channel.volume);
        if (pan != lastPan || volume != lastVolume)
        {
            const auto firstLevels = lastPan < 0;
            const auto send = s.sendPanVolPitch && ! firstLevels;
            if (pan != lastPan)
                panNow.store (pan | (send ? sendBit : 0), std::memory_order_relaxed);
            if (volume != lastVolume)
                volumeNow.store (volume | (send ? sendBit : 0), std::memory_order_relaxed);
            lastPan = pan;
            lastVolume = volume;
            dirty.store (true, std::memory_order_release);
        }
    }

    void setLivePitch (float cents) noexcept override
    {
        const auto bend = bendValue (cents);
        if (bend == lastBend)
            return;
        const auto first = lastBend < 0;
        lastBend = bend;
        bendNow.store (bend | (first ? 0 : sendBit), std::memory_order_relaxed);
        dirty.store (true, std::memory_order_release);
    }

    // automation / a link / a pattern event on FL's parameter k (0 = the patch, 1…72 = the controls)
    void setParameter (int k, float v) noexcept override
    {
        if (k < 0 || k > controlCount || ! std::isfinite (v))
            return;
        adopt();
        if (current == nullptr)
            return;
        if (k == programParam)
        {
            const auto program = programFromNormal (v);
            if (program != automatedProgram)
            {
                automatedProgram = program;
                programPending.store (true, std::memory_order_relaxed);
                dirty.store (true, std::memory_order_release);
            }
            return;
        }
        const auto i = (std::size_t) (k - 1);
        const auto& c = current->state.controls[i];
        if (! c.assigned())
            return;
        // FL: a value from automation or a link switches a deactivated control on again
        if (! c.active)
            forced[i] = true;
        const auto value = valueFromNormal (c, v);
        if (wanted[i].exchange (value, std::memory_order_relaxed) != value)
            dirty.store (true, std::memory_order_release);
    }

    float controlOutput (int) const noexcept override { return 0.0f; }

    void transportStarted() noexcept override { resetPending.store (1, std::memory_order_release); dirty.store (true, std::memory_order_release); }
    void transportStopped() noexcept override { cutPending.store (true, std::memory_order_release); dirty.store (true, std::memory_order_release); }
    void panic() noexcept override { transportStopped(); }

    void requestReset (bool allControllersFirst)
    {
        resetPending.store (allControllersFirst ? 2 : 1, std::memory_order_release);
        dirty.store (true, std::memory_order_release);
    }

    int takeMidiMessages (std::uint32_t* out, int max) noexcept override
    {
        adopt();
        if (current == nullptr || ! dirty.exchange (false, std::memory_order_acquire))
            return 0;
        auto s = effectiveState();
        int n = 0;
        auto room = [&] (int need) {
            if (n + need <= max)
                return true;
            dirty.store (true, std::memory_order_relaxed); // the rest next time
            return false;
        };

        if (cutPending.load (std::memory_order_acquire) && room (controlCount))
        {
            cutPending.store (false, std::memory_order_relaxed);
            if ((s.resetFlags & resetCutNotes) != 0)
                for (int i = 0; i < controlCount; ++i)
                    if (held[(std::size_t) i] >= 0)
                    {
                        const auto& c = s.controls[(std::size_t) i];
                        const auto ch = (c.channel >= 0 ? c.channel : s.channel) & 15;
                        out[n++] = pack (0x80 | ch, held[(std::size_t) i], std::max (0, c.number));
                        held[(std::size_t) i] = -1;
                    }
        }

        const auto pan = takeLevel (panNow), volume = takeLevel (volumeNow), bend = takeLevel (bendNow);
        if (s.sendPanVolPitch && room (4))
        {
            const auto ch = s.channel & 15;
            if (pan.second)
                out[n++] = pack (0xB0 | ch, 10, pan.first);
            if (volume.second)
                out[n++] = pack (0xB0 | ch, 7, volume.first);
            if (bend.second)
                out[n++] = pack (0xE0 | ch, bend.first & 127, (bend.first >> 7) & 127);
        }

        if (const auto reset = resetPending.load (std::memory_order_acquire); reset != 0 && room (maxResetMessages + 1))
        {
            resetPending.store (0, std::memory_order_relaxed);
            programPending.store (false, std::memory_order_relaxed);
            if (reset == 2)
                out[n++] = resetAllControllers (s);
            for (int i = 0; i < controlCount; ++i)
            {
                const auto w = wanted[(std::size_t) i].load (std::memory_order_relaxed);
                if (w != unset)
                    s.values[(std::size_t) i] = w;
                sent[(std::size_t) i] = w;
            }
            n += resetMessages (s, lastSentPan, lastSentVolume, lastSentBend, held, out + n);
        }

        if (programPending.exchange (false, std::memory_order_relaxed) && room (1))
            n += programMessages (s, out + n);

        for (int i = 0; i < controlCount; ++i)
        {
            const auto w = wanted[(std::size_t) i].load (std::memory_order_relaxed);
            if (w == unset || w == sent[(std::size_t) i])
                continue;
            if (! room (6))
                return n;
            sent[(std::size_t) i] = w;
            n += controlMessages (s, i, w, held[(std::size_t) i], out + n);
        }
        return n;
    }

private:
    static constexpr int unset = std::numeric_limits<int>::min();
    static constexpr int sendBit = 1 << 20;

    // the state with the controls automation switched on and the automated patch (the snapshot holds no names: the
    // copy does not allocate)
    State effectiveState() const noexcept
    {
        State s = current->state;
        for (int i = 0; i < controlCount; ++i)
            if (forced[(std::size_t) i] && s.controls[(std::size_t) i].assigned())
                s.controls[(std::size_t) i].active = true;
        if (automatedProgram >= 0)
            s.program = automatedProgram;
        return s;
    }

    std::pair<int, bool> takeLevel (std::atomic<int>& level) noexcept
    {
        const auto v = level.exchange (-1, std::memory_order_relaxed);
        if (v < 0)
            return { -1, false };
        const auto value = v & (sendBit - 1);
        if (&level == &panNow)
            lastSentPan = value;
        else if (&level == &volumeNow)
            lastSentVolume = value;
        else
            lastSentBend = value;
        return { value, (v & sendBit) != 0 };
    }

    void publish (const State& s)
    {
        // collect the snapshot the audio thread let go of, write the next one, hand it over
        auto* fresh = new Snapshot { s };
        for (auto& c : fresh->state.controls)
            c.name.clear(), c.shortName.clear();
        for (auto& name : fresh->state.pageNames)
            name.clear();
        delete retired.exchange (nullptr, std::memory_order_acq_rel);
        delete incoming.exchange (fresh, std::memory_order_acq_rel);
    }

    void adopt() noexcept
    {
        if (retired.load (std::memory_order_acquire) != nullptr)
            return; // (the build thread has not collected the last one yet: keep the current snapshot a little longer)
        if (auto* fresh = incoming.exchange (nullptr, std::memory_order_acq_rel))
        {
            if (current != nullptr)
            {
                // a control switched off in the editor stays off (automation switches it on again)
                for (int i = 0; i < controlCount; ++i)
                    if (current->state.controls[(std::size_t) i].active && ! fresh->state.controls[(std::size_t) i].active)
                        forced[(std::size_t) i] = false;
                if (current->state.program != fresh->state.program) // (the editor's patch wins over automation again)
                    automatedProgram = -1;
            }
            retired.store (current, std::memory_order_release);
            current = fresh;
        }
    }

    // the build thread's own
    bool configured = false;
    State lastState;
    std::array<int, controlCount> songValue {};
    int lastPan = -1, lastVolume = -1;
    // shared
    std::atomic<Snapshot*> incoming { nullptr }, retired { nullptr };
    std::array<std::atomic<int>, controlCount> wanted;
    std::atomic<int> panNow { -1 }, volumeNow { -1 }, bendNow { -1 };
    std::atomic<int> resetPending { 1 }; // (a new MIDI Out sends its setup once, as FL's does when it is created)
    std::atomic<bool> cutPending { false }, programPending { false }, dirty { true };
    // the audio thread's own
    Snapshot* current = nullptr;
    std::array<int, controlCount> sent {}, held {};
    std::array<bool, controlCount> forced {};
    int automatedProgram = -1, lastBend = -1;
    int lastSentPan = -1, lastSentVolume = -1, lastSentBend = -1;

public:
    ~MidiOutDevice() override
    {
        delete incoming.load();
        delete retired.load();
        delete current;
    }
};
} // namespace

std::unique_ptr<Instrument> makeInstrument()
{
    return std::make_unique<MidiOutDevice>();
}

void requestReset (Instrument& instrument, bool allControllersFirst)
{
    if (auto* device = dynamic_cast<MidiOutDevice*> (&instrument))
        device->requestReset (allControllersFirst);
}
} // namespace lynx::midiout
