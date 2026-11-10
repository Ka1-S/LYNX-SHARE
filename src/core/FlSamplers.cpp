#include "core/FlSamplers.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <string>

namespace lynx
{
namespace
{
// chunk = id:u32 size:u64 body (FPC and DirectWave); the body may have sub-chunks
struct Chunk
{
    std::uint32_t id = 0;
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
};

void forEachChunk (const std::uint8_t* p, std::size_t n, const std::function<void (const Chunk&)>& fn)
{
    std::size_t pos = 0;

    while (pos + 12 <= n)
    {
        Chunk c;
        std::uint64_t size = 0;
        std::memcpy (&c.id, p + pos, 4);
        std::memcpy (&size, p + pos + 4, 8);
        pos += 12;

        if (size > n - pos)
            return; // truncated: stop here

        c.data = p + pos;
        c.size = (std::size_t) size;
        fn (c);
        pos += c.size;
    }
}

std::int32_t i32 (const Chunk& c, std::size_t index)
{
    std::int32_t v = 0;
    if ((index + 1) * 4 <= c.size)
        std::memcpy (&v, c.data + index * 4, 4);
    return v;
}

float f32 (const std::uint8_t* p)
{
    float v = 0.0f;
    std::memcpy (&v, p, 4);
    return std::isfinite (v) ? v : 0.0f;
}

std::string ascii (const Chunk& c)
{
    std::string s (reinterpret_cast<const char*> (c.data), c.size);
    s.erase (std::find (s.begin(), s.end(), '\0'), s.end());
    return s;
}

std::string utf16 (const Chunk& c)
{
    std::string out;

    for (std::size_t i = 0; i + 1 < c.size; i += 2)
    {
        const auto ch = (std::uint32_t) c.data[i] | ((std::uint32_t) c.data[i + 1] << 8);

        if (ch == 0)
            break;
        if (ch < 0x80)
            out += (char) ch;
        else if (ch < 0x800)
        {
            out += (char) (0xC0 | (ch >> 6));
            out += (char) (0x80 | (ch & 0x3F));
        }
        else
        {
            out += (char) (0xE0 | (ch >> 12));
            out += (char) (0x80 | ((ch >> 6) & 0x3F));
            out += (char) (0x80 | (ch & 0x3F));
        }
    }

    return out;
}

double l11 (double x) // (11^x − 1)/10: FL's volume curves
{
    return (std::pow (11.0, std::clamp (x, 0.0, 2.0)) - 1.0) / 10.0;
}

// FPC's pad and layer volume law (FPC_x64.dll voice start FUN_0086b480, constant 20.526958f at 0x8E0FA0):
// (exp(v/127 · ln(20.526958 + 1)) − 1)/10, v = the 0…128 (pad) / 0…127 (layer) volume (REA_rodada170i §4)
float fpcVolume (int v)
{
    const float x = (float) v / 127.0f;
    const double e = std::exp ((double) x * std::log ((double) 20.526958f + 1.0));
    return (float) ((double) (float) (e - 1.0) / 10.0);
}
} // namespace

std::optional<std::vector<DrumPad>> fpcPads (const std::vector<std::uint8_t>& d)
{
    if (d.size() < 16)
        return std::nullopt;

    std::uint32_t version = 0;
    std::memcpy (&version, d.data(), 4);

    if (version < 0xF424B || version > 0x1E84E403) // old formats without chunks: not handled
        return std::nullopt;

    std::vector<DrumPad> pads;

    forEachChunk (d.data() + 4, d.size() - 4, [&] (const Chunk& top) {
        if (top.id != 2) // pad
            return;

        DrumPad pad;
        bool muted = false;

        forEachChunk (top.data, top.size, [&] (const Chunk& c) {
            if (c.id == 0x64) // 16 × i32 parameters
            {
                // FPC's own law, and the constant 1/(2·√2) its output carries (measured against FL's renders of
                // pad / layer volume and pan variants of FL's render test, REA_rodada170i §7)
                pad.gain = fpcVolume (std::clamp (i32 (c, 0), 0, 128)) * 0.35355339059327373f;
                pad.pan = std::clamp (i32 (c, 1), -128, 128) / 128.0f;
                muted = (i32 (c, 2) & 1) != 0;
                pad.key = std::clamp (i32 (c, 3), 0, 127);
                pad.mixerOffset = std::clamp (i32 (c, 4), -99, 103);
                pad.cutGroup = i32 (c, 5) >= 0 ? i32 (c, 5) + 1 : 0; // −1 = none; 0 becomes group 1
                pad.cutBy = i32 (c, 6) >= 0 ? i32 (c, 6) + 1 : 0;
                pad.tune = std::clamp (i32 (c, 7), -128, 128) * 9.375f / 100.0f;
            }
            else if (c.id == 0x65)
                pad.name = utf16 (c);
            else if (c.id == 0x69) // layer
            {
                SampleZone layer;
                bool hasPath = false;

                forEachChunk (c.data, c.size, [&] (const Chunk& l) {
                    if (l.id == 0x12C)
                    {
                        layer.path = utf16 (l);
                        hasPath = ! layer.path.empty();
                    }
                    else if (l.id == 0x12D)
                    {
                        layer.gain = fpcVolume (std::clamp (i32 (l, 0), 0, 127));
                        layer.lowVelocity = std::clamp (i32 (l, 2), 0, 127);
                        const auto high = std::clamp (i32 (l, 3), 0, 127); // exclusive, except 127
                        layer.highVelocity = high >= 127 ? 127 : std::max (layer.lowVelocity, high - 1);
                        layer.tune = std::clamp (i32 (l, 5), -128, 128) * 9.375f / 100.0f;
                    }
                });

                if (hasPath)
                    pad.layers.push_back (std::move (layer));
            }
        });

        if (! pad.layers.empty() && ! muted)
        {
            if (pad.name.empty())
                pad.name = "Pad " + std::to_string (pads.size() + 1);
            pads.push_back (std::move (pad));
        }
    });

    if (pads.empty())
        return std::nullopt;

    return pads;
}

std::optional<DirectWaveProgram> directWaveProgram (const std::vector<std::uint8_t>& d)
{
    if (d.size() < 16)
        return std::nullopt;

    std::uint32_t version = 0;
    std::memcpy (&version, d.data(), 4);

    if (version < 36 || version > 38)
        return std::nullopt;

    std::optional<DirectWaveProgram> result;

    forEachChunk (d.data() + 4, d.size() - 4, [&] (const Chunk& top) {
        if (top.id != 1 || result) // the first program with zones
            return;

        DirectWaveProgram program;
        bool envelopeSet = false;

        forEachChunk (top.data, top.size, [&] (const Chunk& z) {
            if (z.id != 3) // zone
                return;

            SampleZone zone;
            float a = 0.0f, dcy = 0.0f, sus = 1.0f, rel = 0.0f;
            bool hasEnvelope = false;
            std::int64_t frames = 0;
            int channels = 0, bits = 0;
            float rate = 44100.0f;
            std::vector<std::uint8_t> embedded;

            forEachChunk (z.data, z.size, [&] (const Chunk& c) {
                if (c.id == 0x1F4 && c.size >= 13) // header: root, keys, velocities, gain
                {
                    zone.rootKey = std::clamp ((int) c.data[0], 0, 127);
                    zone.lowKey = std::clamp ((int) c.data[1], 0, 127);
                    zone.highKey = std::clamp ((int) c.data[2], zone.lowKey, 127);
                    zone.lowVelocity = std::clamp ((int) c.data[3], 0, 127);
                    zone.highVelocity = std::clamp ((int) c.data[4], zone.lowVelocity, 127);
                    const auto g = std::clamp (f32 (c.data + 9), 0.0f, 2.0f);
                    zone.gain = g * g; // gain = v²
                }
                else if (c.id == 0x1F6)
                    zone.path = ascii (c);
                else if (c.id == 0x1F7 && c.size >= 32) // frames, channels, ?, rate, loop mode, loop A, loop B
                {
                    std::memcpy (&frames, c.data, 8);
                    channels = i32 (c, 2);
                    rate = f32 (c.data + 16);
                    if (c.size >= 40)
                        bits = i32 (c, 9);
                    const auto mode = i32 (c, 5);
                    const auto a = (std::int64_t) i32 (c, 6), b = (std::int64_t) i32 (c, 7);

                    // 2 continuous, 3 while held, 4 ping-pong (here: continuous); 0/1 no loop
                    if ((mode == 2 || mode == 3 || mode == 4) && b > a && a >= 0 && (frames <= 0 || b <= frames))
                    {
                        zone.loopStart = a;
                        zone.loopEnd = b;
                        zone.loopMode = mode == 3 ? 2 : 1;
                    }
                }
                else if (c.id == 0x205 && frames > 0 && channels >= 1 && channels <= 2 && (bits == 16 || bits == 32))
                {
                    // embedded audio (REA_rodada9 §2.5): channels in blocks of (frames + 512) samples, with 256
                    // of padding before and after the audio [DATA: exactly 257 zeros at each end]
                    const auto bytes = (std::size_t) bits / 8;
                    const auto block = ((std::size_t) frames + 512) * bytes;
                    if (block * (std::size_t) channels == c.size && std::isfinite (rate) && rate >= 1000.0f && rate <= 384000.0f)
                    {
                        std::vector<std::vector<float>> planar ((std::size_t) channels, std::vector<float> ((std::size_t) frames));
                        for (int ch = 0; ch < channels; ++ch)
                            for (std::int64_t i = 0; i < frames; ++i)
                            {
                                const auto* p = c.data + (std::size_t) ch * block + ((std::size_t) i + 256) * bytes;
                                float v = 0.0f;
                                if (bits == 32)
                                    std::memcpy (&v, p, 4);
                                else
                                {
                                    std::int16_t x = 0;
                                    std::memcpy (&x, p, 2);
                                    v = x / 32768.0f;
                                }
                                planar[(std::size_t) ch][(std::size_t) i] = std::isfinite (v) ? v : 0.0f;
                            }
                        embedded = makeWavFloat (planar, (int) std::lround (rate));
                    }
                }
                else if (c.id == 0x1F8 && c.size >= 4)
                    zone.tune = (std::clamp (f32 (c.data), 0.0f, 1.0f) - 0.5f) * 24.0f; // (v − 0.5)·2400 cents
                else if (c.id == 0x1FD && c.size >= 16)
                {
                    auto time = [] (float v) { return 10.0f * std::pow (std::clamp (v, 0.0f, 1.0f), 4.0f); }; // 10·v⁴ s
                    a = time (f32 (c.data));
                    dcy = time (f32 (c.data + 4));
                    sus = std::clamp (f32 (c.data + 8), 0.0f, 1.0f);
                    rel = time (f32 (c.data + 12));
                    hasEnvelope = true;
                }
            });

            if (zone.path.empty())
                return;

            if (hasEnvelope && ! envelopeSet)
            {
                program.attack = a;
                program.decay = dcy;
                program.sustain = sus;
                program.release = rel;
                envelopeSet = true;
            }

            program.zones.push_back (std::move (zone));
            program.embeddedWav.push_back (std::move (embedded));
        });

        if (! program.zones.empty())
            result = std::move (program);
    });

    return result;
}
namespace
{
// bounded sequential reader (the blob may be truncated)
struct Stream
{
    const std::vector<std::uint8_t>& d;
    std::size_t pos = 0;
    bool ok = true;

    template <typename T>
    T read()
    {
        T v {};
        if (pos + sizeof (T) > d.size())
        {
            ok = false;
            return v;
        }
        std::memcpy (&v, d.data() + pos, sizeof (T));
        pos += sizeof (T);
        return v;
    }

    std::string str8() // u8 length + ANSI bytes
    {
        const auto n = read<std::uint8_t>();
        if (! ok || pos + n > d.size())
        {
            ok = false;
            return {};
        }
        std::string s (reinterpret_cast<const char*> (d.data() + pos), n);
        pos += n;
        return s;
    }

    void skip (std::size_t n)
    {
        if (pos + n > d.size())
            ok = false;
        else
            pos += n;
    }
};
} // namespace

std::optional<SlicedSample> fruitySlicer (const std::vector<std::uint8_t>& d)
{
    Stream in { d };
    const auto version = in.read<std::uint32_t>();

    if (version < 13 || version > 16)
        return std::nullopt;

    SlicedSample result;
    in.skip (8); // length in beats, original BPM
    result.pitchCents = (float) std::clamp (in.read<std::int32_t>(), -1200, 1200);
    in.skip (16); // stretching, method, fades
    result.path = in.str8();
    const auto count = in.read<std::uint32_t>();

    if (! in.ok || count > 4096)
        return std::nullopt;

    for (std::uint32_t i = 0; i < count && in.ok; ++i)
    {
        in.str8(); // name
        const auto position = in.read<std::int32_t>();
        in.skip (4 + 4 + 1);
        if (in.ok && position >= 0)
            result.positions.push_back (position);
    }

    if (! in.ok || result.path.empty() || result.positions.empty())
        return std::nullopt;

    std::sort (result.positions.begin(), result.positions.end());
    return result;
}

std::optional<SlicedSample> slicexSample (const std::vector<std::uint8_t>& d)
{
    Stream in { d };
    const auto version = in.read<std::uint32_t>();

    if (version != 0x00060003u && version != 6u)
        return std::nullopt;

    SlicedSample result;
    std::int32_t master[8] {};
    for (auto& m : master)
        m = in.read<std::int32_t>();
    result.pitchCents = (float) std::clamp (master[5], -1200, 1200);
    // master volume (DFM range 0…16000, 12800 = 100 %): FL's L11 law in single precision (Slicex FUN_0087efe0)
    {
        const auto x = (float) std::clamp (master[2], 0, 16000) * 7.8125e-05f;
        result.gain = x == 1.0f ? 1.0f : (float) ((std::exp ((double) (x * (float) std::log (11.0))) - 1.0) * (double) 0.1f);
    }
    in.skip (47 + 12); // options and UI state: deck A starts at 0x5F

    const auto referenceOnly = in.read<std::uint8_t>(); // 0 = embedded WAV follows
    result.path = in.str8();

    if (in.ok && referenceOnly == 0)
    {
        const auto size = in.read<std::uint32_t>();
        if (in.ok && size > 0 && in.pos + size <= d.size())
            result.embeddedWav.assign (d.begin() + (std::ptrdiff_t) in.pos, d.begin() + (std::ptrdiff_t) (in.pos + size));
    }

    if (! in.ok || (result.path.empty() && result.embeddedWav.empty()))
        return std::nullopt;

    return result;
}

std::vector<std::int64_t> wavCuePoints (const std::vector<std::uint8_t>& w, std::int64_t& frames)
{
    std::vector<std::int64_t> cues;
    frames = 0;

    if (w.size() < 12 || std::memcmp (w.data(), "RIFF", 4) != 0 || std::memcmp (w.data() + 8, "WAVE", 4) != 0)
        return cues;

    std::uint16_t format = 0, blockAlign = 0;
    std::size_t pos = 12;

    while (pos + 8 <= w.size())
    {
        std::uint32_t size = 0;
        std::memcpy (&size, w.data() + pos + 4, 4);
        const auto* body = w.data() + pos + 8;
        const auto available = std::min<std::size_t> (size, w.size() - pos - 8);

        if (std::memcmp (w.data() + pos, "fmt ", 4) == 0 && available >= 16)
        {
            std::memcpy (&format, body, 2);
            std::memcpy (&blockAlign, body + 12, 2);
        }
        else if (std::memcmp (w.data() + pos, "data", 4) == 0 && blockAlign > 0 && (format == 1 || format == 3 || format == 0xFFFE))
            frames = (std::int64_t) (size / blockAlign);
        else if (std::memcmp (w.data() + pos, "cue ", 4) == 0 && available >= 4)
        {
            std::uint32_t count = 0;
            std::memcpy (&count, body, 4);

            for (std::uint32_t i = 0; i < count && 4 + (std::size_t) (i + 1) * 24 <= available; ++i)
            {
                std::uint32_t sampleOffset = 0;
                std::memcpy (&sampleOffset, body + 4 + (std::size_t) i * 24 + 20, 4);
                cues.push_back (sampleOffset);
            }
        }

        pos += 8 + size + (size & 1); // chunks aligned to 2
    }

    std::sort (cues.begin(), cues.end());
    cues.erase (std::unique (cues.begin(), cues.end()), cues.end());
    return cues;
}
std::optional<std::string> flKeysSet (const std::vector<std::uint8_t>& d)
{
    Stream in { d };

    if (in.read<std::uint32_t>() != 1)
        return std::nullopt;

    auto name = in.str8();
    name.erase (std::find (name.begin(), name.end(), '\0'), name.end());

    if (! in.ok || name.empty() || name.find_first_of ("\\/:") != std::string::npos)
        return std::nullopt;

    return name;
}

std::vector<std::uint8_t> makeWav16Mono (const std::int16_t* samples, std::size_t count, int sampleRate)
{
    std::vector<std::uint8_t> wav;
    auto u32 = [&] (std::uint32_t v) { for (int i = 0; i < 4; ++i) wav.push_back ((std::uint8_t) (v >> (8 * i))); };
    auto u16 = [&] (std::uint16_t v) { wav.push_back ((std::uint8_t) v); wav.push_back ((std::uint8_t) (v >> 8)); };
    auto tag = [&] (const char* t) { wav.insert (wav.end(), t, t + 4); };
    const auto bytes = (std::uint32_t) (count * 2);
    tag ("RIFF"); u32 (36 + bytes); tag ("WAVE");
    tag ("fmt "); u32 (16); u16 (1); u16 (1); u32 ((std::uint32_t) sampleRate); u32 ((std::uint32_t) sampleRate * 2); u16 (2); u16 (16);
    tag ("data"); u32 (bytes);
    const auto* raw = reinterpret_cast<const std::uint8_t*> (samples);
    wav.insert (wav.end(), raw, raw + bytes);
    return wav;
}

std::vector<std::uint8_t> makeWavFloat (const std::vector<std::vector<float>>& channels, int sampleRate)
{
    std::vector<std::uint8_t> wav;
    if (channels.empty())
        return wav;
    const auto count = channels.front().size();
    const auto n = (std::uint16_t) channels.size();
    auto u32 = [&] (std::uint32_t v) { for (int i = 0; i < 4; ++i) wav.push_back ((std::uint8_t) (v >> (8 * i))); };
    auto u16 = [&] (std::uint16_t v) { wav.push_back ((std::uint8_t) v); wav.push_back ((std::uint8_t) (v >> 8)); };
    auto tag = [&] (const char* t) { wav.insert (wav.end(), t, t + 4); };
    const auto bytes = (std::uint32_t) (count * n * 4);
    tag ("RIFF"); u32 (36 + bytes); tag ("WAVE");
    tag ("fmt "); u32 (16); u16 (3); u16 (n); u32 ((std::uint32_t) sampleRate); u32 ((std::uint32_t) sampleRate * n * 4); u16 ((std::uint16_t) (n * 4)); u16 (32);
    tag ("data"); u32 (bytes);
    wav.reserve (wav.size() + bytes);
    for (std::size_t i = 0; i < count; ++i)
        for (const auto& ch : channels)
        {
            const auto v = i < ch.size() ? ch[i] : 0.0f;
            const auto* raw = reinterpret_cast<const std::uint8_t*> (&v);
            wav.insert (wav.end(), raw, raw + 4);
        }
    return wav;
}

std::vector<SampleZone> flKeysZones (const std::string& wti, const std::string& folder, const std::vector<std::uint8_t>& rawData,
                                     const std::function<std::string (const std::vector<std::uint8_t>&, const std::string&)>& saveWav)
{
    // simple INI: [section] and key=value
    std::map<std::string, std::map<std::string, std::string>> ini;
    std::string section;
    std::size_t start = 0;

    while (start < wti.size())
    {
        auto end = wti.find ('\n', start);
        if (end == std::string::npos)
            end = wti.size();
        auto line = wti.substr (start, end - start);
        start = end + 1;
        line.erase (std::remove (line.begin(), line.end(), '\r'), line.end());

        if (line.size() >= 2 && line.front() == '[' && line.back() == ']')
        {
            section = line.substr (1, line.size() - 2);
            std::transform (section.begin(), section.end(), section.begin(), [] (unsigned char ch) { return (char) std::tolower (ch); });
        }
        else if (const auto eq = line.find ('='); eq != std::string::npos)
            ini[section][line.substr (0, eq)] = line.substr (eq + 1);
    }

    auto value = [&] (const std::string& sec, const std::string& key, int fallback) {
        const auto s = ini.find (sec);
        if (s == ini.end())
            return fallback;
        const auto k = s->second.find (key);
        if (k == s->second.end() || k->second.empty())
            return fallback;
        try { return std::stoi (k->second); } catch (...) { return fallback; }
    };
    auto text = [&] (const std::string& sec, const std::string& key) {
        const auto s = ini.find (sec);
        if (s == ini.end())
            return std::string();
        const auto k = s->second.find (key);
        return k == s->second.end() ? std::string() : k->second;
    };

    const auto count = std::clamp (value ("info", "samples", 0), 0, 512);
    const auto raw = text ("info", "filetype") == "raw";
    const auto rate = std::clamp (value ("info", "samplerate", 44100), 8000, 192000);
    const auto subfolder = text ("info", "path");
    const auto layers = std::clamp (value ("velocitylayers", "count", 1), 1, 16);
    std::vector<int> thresholds;
    for (int l = 0; l < layers; ++l)
        thresholds.push_back (std::clamp (value ("velocitylayers", std::to_string (l), 0), 0, 127));

    std::vector<SampleZone> zones;
    std::string lastName;

    for (int i = 0; i < count; ++i)
    {
        const auto sec = "sample" + std::to_string (i);
        SampleZone zone;
        zone.rootKey = zone.lowKey = std::clamp (value (sec, "root", 60), 0, 127); // the root is also the start of the range
        zone.highKey = std::clamp (value (sec, "high", 127), zone.lowKey, 127);
        const auto layer = i % layers;
        zone.lowVelocity = thresholds[(std::size_t) layer];
        zone.highVelocity = layer + 1 < layers ? std::max (zone.lowVelocity, thresholds[(std::size_t) layer + 1] - 1) : 127;

        if (raw)
        {
            const auto from = (std::size_t) std::max (0, value (sec, "pos", 0));
            const auto to = (std::size_t) std::max (0, value (sec, "end", 0));

            if (to <= from || to * 2 > rawData.size() || ! saveWav)
                continue;

            const auto wav = makeWav16Mono (reinterpret_cast<const std::int16_t*> (rawData.data()) + from, to - from, rate);
            zone.path = saveWav (wav, "FL Keys " + std::to_string (i));
        }
        else
        {
            if (const auto name = text (sec, "name"); ! name.empty())
                lastName = name;
            if (lastName.empty())
                continue;
            zone.path = folder + "\\" + (subfolder.empty() ? std::string() : subfolder + "\\") + lastName;
        }

        if (! zone.path.empty())
            zones.push_back (std::move (zone));
    }

    return zones;
}
} // namespace lynx
