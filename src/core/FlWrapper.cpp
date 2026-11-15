#include "core/FlWrapper.h"

#include "flp/Flp.h"

#include <algorithm>
#include <cstring>
#include <string>

namespace lynx
{
namespace
{
struct Reader
{
    const std::vector<std::uint8_t>& data;
    std::size_t pos = 0;

    std::size_t remaining() const noexcept { return data.size() - pos; }

    template <typename T>
    T read() noexcept
    {
        T v {};
        std::memcpy (&v, data.data() + pos, sizeof (T)); // little-endian, like the x86 that wrote it
        pos += sizeof (T);
        return v;
    }
};

std::uint32_t u32At (const std::vector<std::uint8_t>& d, std::size_t offset)
{
    std::uint32_t v = 0;
    if (offset + 4 <= d.size())
        std::memcpy (&v, d.data() + offset, 4);
    return v;
}

// unterminated text: UTF-8 from version 9 on; before that, ANSI (Windows-1252 ~ Latin-1 in ASCII)
std::string text (const std::uint8_t* p, std::size_t n, bool utf8)
{
    std::string s;

    for (std::size_t i = 0; i < n && p[i] != 0; ++i)
    {
        const auto c = p[i];

        if (utf8 || c < 0x80)
            s += (char) c;
        else // Latin-1 -> UTF-8 (approximates 1252 outside 0x80..0x9F)
        {
            s += (char) (0xC0 | (c >> 6));
            s += (char) (0x80 | (c & 0x3F));
        }
    }

    return s;
}

// walks {u32 id, u64 size, data}; stops at a truncated record
template <typename Fn>
void forEachRecord (Reader& r, Fn&& fn)
{
    while (r.remaining() >= 12)
    {
        const auto id = r.read<std::uint32_t>();
        const auto size = r.read<std::uint64_t>();

        if (size > r.remaining())
            return;

        fn (id, r.data.data() + r.pos, (std::size_t) size);
        r.pos += (std::size_t) size;
    }
}
} // namespace

bool parseFlWrapper (const std::vector<std::uint8_t>& blob, ForeignPlugin& out)
{
    if (blob.size() < 4)
        return false;

    Reader r { blob };
    const auto version = r.read<std::uint32_t>();

    if (version < 5 || version > 12)
        return false;

    const auto utf8 = version >= 9;
    std::uint32_t type = 0xFFFFFFFFu;
    out = {};
    out.wrapperVersion = version;

    forEachRecord (r, [&] (std::uint32_t id, const std::uint8_t* p, std::size_t n) {
        switch (id)
        {
            case 0x32: if (n >= 4) std::memcpy (&type, p, 4); break;
            case 0x33: if (n >= 4) std::memcpy (&out.vst2Id, p, 4); break;
            case 0x34:
                if (n >= 16)
                {
                    std::copy (p, p + 16, out.classId.begin());
                    out.hasClassId = std::any_of (out.classId.begin(), out.classId.end(), [] (auto b) { return b != 0; });
                }
                break;
            case 0x3A: out.clapId = text (p, n, utf8); break;
            case 0x36: out.name = text (p, n, utf8); break;
            case 0x37: out.path = text (p, n, utf8); break;
            case 0x35: out.state.assign (p, p + n); break;
            case 0x01: // MIDI input and output ports (REA_rodada16 §1.2)
                if (n >= 8)
                {
                    std::memcpy (&out.midiInPort, p, 4);
                    std::memcpy (&out.midiOutPort, p + 4, 4);
                }
                if (n >= 12) // "Send pitch bend range (semitones)" value (round 151)
                    std::memcpy (&out.bendRange, p + 8, 4);
                break;
            case 0x02: // wrapper options (Fruity Wrapper FUN_00874480 writes, FUN_00875aa0 reads; REA round 134)
                if (n >= 17)
                {
                    std::memcpy (&out.wrapperFlags, p + 9, 4);
                    std::memcpy (&out.fixedOptions, p + 13, 4);
                    out.hasWrapperOptions = true;
                    if (n >= 25)
                    {
                        std::memcpy (&out.wrapperFlags2, p + 21, 4);
                        out.hasWrapperFlags2 = true;
                    }
                }
                break;
            case 0x1F: // Processing › Connections, round 188d [DATA]: 12 bytes per input / output
            case 0x20:
            {
                auto& list = id == 0x1F ? out.inputRoutes : out.outputRoutes;
                list.clear();
                for (std::size_t k = 0; k + 12 <= n && list.size() < 256; k += 12)
                {
                    std::int32_t offset = 0;
                    std::uint32_t active = 1;
                    std::memcpy (&offset, p + k, 4);
                    std::memcpy (&active, p + k + 4, 4);
                    list.push_back ({ std::clamp (offset, -999, 999), (std::int8_t) (active != 0 ? 1 : 0) });
                }
                out.hasRoutes = true;
                break;
            }
            default: break; // 0x1E (the counts): the lists carry them
        }
    });

    switch (type)
    {
        case 0:
        case 4: out.format = ForeignPlugin::Format::vst2; break;
        case 7:
        case 8: out.format = ForeignPlugin::Format::vst3; break;
        case 11:
        case 12: out.format = ForeignPlugin::Format::clap; break;
        case 0xFFFFFFFFu: return false; // without the type record the plugin cannot be found
        default: out.format = ForeignPlugin::Format::other; break; // DirectX, AU, native FL
    }

    return true;
}

void applyFlWrapperOptions (const ForeignPlugin& w, PluginRef& ref)
{
    // Processing › Connections (round 188d): the inputs and outputs as FL routed them (a generator's outputs: mixer
    // track offsets; an effect's: sidechain indexes)
    if (w.hasRoutes)
    {
        ref.outputs = w.outputRoutes;
        ref.inputs = w.inputRoutes;
        trimOutputRoutes (ref.outputs);
        trimOutputRoutes (ref.inputs);
    }
    if (! w.hasWrapperOptions || (w.wrapperFlags & 0x400000u) != 0)
        return;
    // "Process inactive inputs and outputs" (flag 0x8, round 188d [DATA: mo-a / mo-b saves])
    if (w.wrapperVersion >= 10)
        ref.processInactive = (w.wrapperFlags & 0x8u) != 0;
    ref.fixedBuffers = (w.wrapperFlags & 0x2u) != 0;
    ref.notifyRender = (w.wrapperFlags & 0x4u) != 0; // "Notify about rendering mode" (REA round 134)
    ref.maxSizeBuffers = ref.fixedBuffers && (w.fixedOptions & 0x1u) != 0;
    if (w.wrapperVersion >= 7)
        ref.threaded = (w.wrapperFlags & 0x1000u) != 0;
    if (w.wrapperVersion >= 8)
        ref.separateProcess = (w.wrapperFlags & 0x40000u) != 0;
    if (w.wrapperVersion >= 10)
        ref.allowSmartDisable = (w.wrapperFlags & 0x20000000u) != 0;
    // "Use maximum buffer size from host" (fixed-buffer options bit 1): the plugin is told the host's maximum block
    ref.hostMaxBuffer = ref.fixedBuffers && (w.fixedOptions & 0x2u) != 0;
    // round 151 [DATA: FL 2026 (wrapper version 12), each option toggled, saved and compared]: 0x1 "Send pitch bend
    // range", 0x20 set = "Send note release velocity" off, 0x800 "Send loop position", 0x20000 "Send MOD X as
    // polyphonic aftertouch", 0x02000000 set = "Reset plugin when FL Studio resets" off; the second word's 0x1000 set
    // = "Send All Notes Off when playback stops" off
    if (w.wrapperVersion >= 10)
    {
        ref.sendBendRange = (w.wrapperFlags & 0x1u) != 0;
        ref.releaseVelocity = (w.wrapperFlags & 0x20u) == 0;
        ref.sendLoopPosition = (w.wrapperFlags & 0x800u) != 0;
        ref.modXAftertouch = (w.wrapperFlags & 0x20000u) != 0;
        ref.resetOnFlush = (w.wrapperFlags & 0x02000000u) == 0;
        // the window's Typing keyboard priority button off [DATA round 187a: kb-on.flp / kb-off2.flp, 0xB4007D8C -> 0xB5007D8C]
        ref.hostKeysFirst = (w.wrapperFlags & 0x01000000u) != 0;
        if (w.hasWrapperFlags2)
        {
            ref.allNotesOffOnStop = (w.wrapperFlags2 & 0x1000u) == 0;
            // "Allow FL Studio to process keyboard presses when the keyboard focus option is on" [DATA: the same project saved with it off and on, only this bit differs]
            ref.allowHostKeys = (w.wrapperFlags2 & 0x800u) != 0;
        }
    }
    if (w.bendRange >= 1 && w.bendRange <= 48)
        ref.bendRange = w.bendRange;
}

bool splitFlVst3State (const std::vector<std::uint8_t>& state, std::vector<std::uint8_t>& component,
                       std::vector<std::uint8_t>& controller)
{
    component.clear();
    controller.clear();

    if (state.size() < 4 || u32At (state, 0) > 1)
        return false;

    Reader r { state, 4 };
    forEachRecord (r, [&] (std::uint32_t id, const std::uint8_t* p, std::size_t n) {
        if (id == 3)
            component.assign (p, p + n);
        else if (id == 2)
            controller.assign (p, p + n);
    });

    return ! component.empty() || ! controller.empty();
}

std::vector<std::int32_t> flVst3ParamSlots (const std::vector<std::uint8_t>& state)
{
    std::vector<std::int32_t> slots;

    if (state.size() < 4 || u32At (state, 0) > 1)
        return slots;

    Reader r { state, 4 };
    forEachRecord (r, [&] (std::uint32_t id, const std::uint8_t* p, std::size_t n) {
        if (id != 4 || n < 4)
            return;
        std::uint32_t count = 0;
        std::memcpy (&count, p, 4);
        if (count > (n - 4) / 4)
            return;
        slots.resize (count);
        std::memcpy (slots.data(), p + 4, count * 4);
    });

    return slots;
}

std::vector<int> flVst3SlotPositions (const std::vector<std::int32_t>& savedSlots, const std::vector<std::uint32_t>& paramIds)
{
    std::vector<int> positions (savedSlots.size(), -1);
    std::vector<bool> hasSlot (paramIds.size(), false);

    for (std::size_t slot = 0; slot < savedSlots.size(); ++slot)
    {
        if (savedSlots[slot] == -1)
            continue;
        for (std::size_t i = 0; i < paramIds.size(); ++i)
            if (paramIds[i] == (std::uint32_t) savedSlots[slot] && ! hasSlot[i])
            {
                positions[slot] = (int) i;
                hasSlot[i] = true;
                break;
            }
    }

    for (std::size_t i = 0; i < paramIds.size(); ++i)
        if (! hasSlot[i])
            positions.push_back ((int) i);

    return positions;
}

bool parseFlVst2State (const std::vector<std::uint8_t>& state, FlVst2State& out)
{
    out = {};
    std::size_t pos = 0;
    bool ok = true;
    auto take = [&] (void* dst, std::size_t n) {
        if (! ok || pos + n > state.size())
        {
            ok = false;
            return;
        }
        std::memcpy (dst, state.data() + pos, n);
        pos += n;
    };
    auto i32 = [&] {
        std::int32_t v = 0;
        take (&v, 4);
        return v;
    };
    auto size = [&] {
        const auto marker = i32();
        if (marker != -2)
            return (std::int64_t) marker;
        std::int64_t v = 0;
        take (&v, 8);
        return v;
    };

    const auto v = i32();
    if (! ok || v >= 0 || v < -9)
        return false; // ≥ 0 (and the 123456 marker): older layouts, not decoded
    out.version = v;

    std::int64_t chunkSize = -3; // FL's "not read yet"
    if (v <= -6)
        take (&out.flags, 1);
    if (v <= -5)
        chunkSize = size();
    if (v == -7 && chunkSize == 0)
        i32(); // an extra count only this version writes (its meaning is not confirmed)
    if (v <= -6)
        out.currentProgram = i32();
    if (v == -5 || v == -4)
        out.currentProgram = i32();
    if (v >= -4)
        chunkSize = size();
    if (! ok || chunkSize > (std::int64_t) (state.size() - pos))
        return false;
    if (chunkSize > 0)
    {
        out.chunk.assign (state.begin() + (std::ptrdiff_t) pos, state.begin() + (std::ptrdiff_t) (pos + (std::size_t) chunkSize));
        pos += (std::size_t) chunkSize;
    }

    // raw parameters: read only when there is no chunk (or for version −2)
    if (v == -2 || (v <= -5 && (chunkSize == 0 || chunkSize == -1)))
    {
        const auto n = i32();
        if (! ok || n < 0 || (std::size_t) n > (state.size() - pos) / 4)
            return false;
        out.params.resize ((std::size_t) n);
        take (out.params.data(), (std::size_t) n * 4);
    }

    // program names (optional at the end)
    if (ok && pos + 4 <= state.size())
    {
        const auto m = i32();
        const auto names = m <= 0 ? 0 : (v <= -6 ? 1 : m);
        for (int i = 0; i < names && pos + 25 <= state.size(); ++i)
        {
            out.programNames.push_back (text (state.data() + pos, 25, false));
            pos += 25;
        }
    }

    return ok;
}

std::vector<std::uint8_t> writeFlVst2State (const FlVst2State& s)
{
    std::vector<std::uint8_t> out;
    auto put = [&] (const void* p, std::size_t n) { out.insert (out.end(), (const std::uint8_t*) p, (const std::uint8_t*) p + n); };
    auto i32 = [&] (std::int32_t v) { put (&v, 4); };

    i32 (-9);
    put (&s.flags, 1);
    i32 (-2);
    const auto size = (std::int64_t) s.chunk.size();
    put (&size, 8);
    i32 (s.currentProgram);
    if (! s.chunk.empty())
    {
        put (s.chunk.data(), s.chunk.size()); // FL writes nothing after the chunk
        return out;
    }
    i32 ((std::int32_t) s.params.size());
    if (! s.params.empty())
        put (s.params.data(), s.params.size() * 4);
    i32 (s.programNames.empty() ? 0 : 1);
    if (! s.programNames.empty())
    {
        char name[25] {};
        std::memcpy (name, s.programNames.front().data(), std::min<std::size_t> (24, s.programNames.front().size()));
        put (name, 25);
    }
    return out;
}

const std::vector<WrapperOption>& wrapperOptions()
{
    // in FL's order (Fruity Wrapper's settings form, REA round 149): Processing › Options, then Troubleshooting ›
    // Compatibility
    static const std::vector<WrapperOption> options = {
        { "Processing", "Notify about rendering mode", "Let the plugin know when rendering to an audio file", &PluginRef::notifyRender, false, false },
        { "Processing", "Allow threaded processing", "Allow the plugin to be multi-threaded (off: always on the audio thread)", &PluginRef::threaded, false, false },
        { "Processing", "Make bridged", "Load the plugin in a separate process (bridged)", &PluginRef::separateProcess, true, false },
        { "Processing", "Allow smart disable", "Allow smart disable for the plugin (off: it is always processed)", &PluginRef::allowSmartDisable, false, false },
        // round 188d [EXEC]: on, an output whose LED is off still plays on its insert
        { "Processing", "Process inactive inputs and outputs", "Process inputs and outputs marked as inactive", &PluginRef::processInactive, false, false },
        { "MIDI", "Send note release velocity", "Send release velocity in note off messages", &PluginRef::releaseVelocity, true, false },
        { "MIDI", "Send pitch bend range (semitones)", "Send a pitch bend range MIDI RPN", &PluginRef::sendBendRange, true, false },
        { "MIDI", "Send MOD X as polyphonic aftertouch", "Send each note's Modulation X as polyphonic aftertouch", &PluginRef::modXAftertouch, true, false },
        // issue #108 (a LYNX addition; FL's wrapper has no MPE switch): beside the other per-note MIDI options
        { "MIDI", "MPE", "MIDI Polyphonic Expression: each note on its own MIDI channel with its fine pitch, Mod X (timbre) and Mod Y (pressure)", &PluginRef::mpe, true, false },
        { "MIDI", "Send All Notes Off when playback stops", "Off: only the notes still held get their note off", &PluginRef::allNotesOffOnStop, true, false },
        { "Compatibility", "Reset plugin when LYNX resets", "Reset the plugin before an export and on panic (clears its tails and held notes)", &PluginRef::resetOnFlush, true, false },
        { "Compatibility", "Send loop position", "Let the plugin know about loop start and end points", &PluginRef::sendLoopPosition, true, false },
        { "Compatibility", "Use fixed size buffers", "Constant 2 ms blocks, for plugins that need equal buffers (adds latency)", &PluginRef::fixedBuffers, true, false },
        { "Compatibility", "Process maximum size buffers", "With fixed size buffers: the whole device buffer instead of 2 ms blocks", &PluginRef::maxSizeBuffers, true, true },
        { "Compatibility", "Use maximum buffer size from host", "With fixed size buffers: prepare the plugin for the host's largest block", &PluginRef::hostMaxBuffer, true, true },
    };
    return options;
}

void trimOutputRoutes (std::vector<PluginOutputRoute>& routes)
{
    while (! routes.empty() && routes.back() == PluginOutputRoute {})
        routes.pop_back();
}

void autoMapOutputs (std::vector<PluginOutputRoute>& routes, int count)
{
    if (count <= 0)
        return;
    if ((int) routes.size() < count)
        routes.resize ((std::size_t) count);
    const auto first = routes.front().offset;
    for (int k = 0; k < count; ++k)
        routes[(std::size_t) k].offset = std::clamp (first + k, -999, 999);
    trimOutputRoutes (routes);
}

void resetOutputOffsets (std::vector<PluginOutputRoute>& routes, int count)
{
    for (int k = 0; k < std::min (count, (int) routes.size()); ++k)
        routes[(std::size_t) k].offset = 0;
    trimOutputRoutes (routes);
}

int outputTargetTrack (int channelTrack, const PluginOutputRoute& route, bool pluginDefault) noexcept
{
    return route.isActive (pluginDefault) ? channelTrack + route.offset : -1;
}

bool foreignFromFlPreset (const std::vector<std::uint8_t>& fileBytes, ForeignPlugin& out)
{
    try
    {
        // a channel or effect preset holds one plugin record: its name (0xC9) and state (0xD5), as a channel
        const auto project = flp::parseProject (fileBytes, true);
        if (project.channels.empty() || project.channels.front().pluginName != "Fruity Wrapper")
            return false;
        return parseFlWrapper (project.channels.front().pluginState, out) && ! out.name.empty();
    }
    catch (const std::exception&)
    {
        return false;
    }
}
} // namespace lynx
