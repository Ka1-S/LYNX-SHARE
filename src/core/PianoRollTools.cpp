#include "core/PianoRollTools.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <map>
#include <random>
#include <set>

#include "flp/Flp.h"

namespace lynx::prtools
{
namespace
{
constexpr std::int64_t beatTicks = ppq;
constexpr std::int64_t minimumLength = ppq / 24; // FL's minimum note length (1/24 beat; REA_rodada83 §0)

// FL's Round (round half to even, like x64 cvtsd2si)
std::int64_t roundEven (double v) noexcept
{
    return (std::int64_t) std::nearbyint (v);
}

// a / b rounded toward zero, like Delphi's div
std::int64_t divTrunc (std::int64_t a, std::int64_t b) noexcept
{
    return a / b;
}

// floor division and modulo (for positions, which are never negative in FL)
std::int64_t floorDiv (std::int64_t a, std::int64_t b) noexcept
{
    auto q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0)))
        --q;
    return q;
}

std::int64_t floorMod (std::int64_t a, std::int64_t b) noexcept
{
    return a - floorDiv (a, b) * b;
}

std::vector<int> validSorted (const Pattern& p, std::vector<int> indices)
{
    indices.erase (std::remove_if (indices.begin(), indices.end(), [&p] (int i) { return i < 0 || i >= (int) p.notes.size(); }),
                   indices.end());
    std::sort (indices.begin(), indices.end());
    indices.erase (std::unique (indices.begin(), indices.end()), indices.end());
    return indices;
}

// Takes the notes at `indices` out of the pattern (sorted by start, stable), lets `fn` rewrite them, puts the
// result back and re-sorts the pattern.
template <typename Fn>
void rewrite (Pattern& pattern, const std::vector<int>& indices, Fn&& fn)
{
    const auto valid = validSorted (pattern, indices);

    if (valid.empty())
        return;

    std::vector<Note> work;
    work.reserve (valid.size());

    for (auto i : valid)
        work.push_back (pattern.notes[(std::size_t) i]);

    std::stable_sort (work.begin(), work.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });

    for (auto it = valid.rbegin(); it != valid.rend(); ++it)
        pattern.notes.erase (pattern.notes.begin() + *it);

    fn (work);

    pattern.notes.insert (pattern.notes.end(), work.begin(), work.end());
    pattern.sortNotes();
}

// FL's level blend toward a template note (quantizer/chopper): velocity target = template x note / 100 (FL bytes),
// others target = template + (note - default); value += (target - value) x strength.
void blendLevels (Note& n, const ScoreNote& t, const LevelStrengths& s)
{
    const auto blend = [] (float value, float target, float strength) { return value + (target - value) * strength; };

    const auto velocityTarget = t.velocity * 128.0f / 100.0f * n.velocity;
    n.velocity = std::clamp (blend (n.velocity, velocityTarget, s.velocity), 0.0f, 1.0f);
    n.pan = std::clamp (blend (n.pan, t.pan + n.pan, s.pan), -1.0f, 1.0f);
    n.release = std::clamp (blend (n.release, t.release + (n.release - 0.5f), s.release), 0.0f, 1.0f);
    n.finePitch = std::clamp (blend (n.finePitch, t.finePitch + n.finePitch, s.pitch), -120.0f, 120.0f);
    n.modX = std::clamp (blend (n.modX, t.modX + (n.modX - 128.0f / 255.0f), s.modX), 0.0f, 1.0f);
    n.modY = std::clamp (blend (n.modY, t.modY + (n.modY - 128.0f / 255.0f), s.modY), 0.0f, 1.0f);
}

std::int64_t barTicks (Meter meter) noexcept
{
    return meter.bar();
}
} // namespace

// --- score templates --------------------------------------------------------------------------

std::int64_t Score::length() const noexcept
{
    std::int64_t end = 0;

    for (const auto& n : notes)
        end = std::max (end, n.start + std::max<std::int64_t> (1, n.length));

    return end;
}

std::optional<Score> parseScore (std::span<const std::uint8_t> bytes)
{
    // An .fsc is an .flp holding one pattern's notes (event 0xE0). Files written before FL 5 use 20-byte note
    // records (no group/release); colours of files before 5.3.0 are stored >> 2 (REA_rodada2 §4.1).
    try
    {
        flp::EventReader reader (bytes);
        const auto filePpq = std::max<int> (1, reader.header().ppq);
        const auto rescale = [filePpq] (std::int64_t t) { return roundEven ((double) t * ppq / filePpq); };
        std::uint32_t version = 0;
        flp::Event e;
        Score score;

        while (reader.next (e))
        {
            if (e.id == 0xC7 && e.isData())
                version = flp::parseVersionNumber (std::string (e.data.begin(), std::find (e.data.begin(), e.data.end(), (std::uint8_t) 0)));

            if (e.id != 0xE0 || ! e.isData() || ! score.notes.empty())
                continue;

            const auto oldRecords = version != 0 && version < 5000000u;
            const std::size_t size = oldRecords ? 20 : 24;

            if (e.data.size() % size != 0)
                continue;

            const auto u16 = [&e] (std::size_t o) { return (int) (e.data[o] | (e.data[o + 1] << 8)); };
            const auto u32 = [&e] (std::size_t o) {
                return (std::uint32_t) e.data[o] | ((std::uint32_t) e.data[o + 1] << 8) | ((std::uint32_t) e.data[o + 2] << 16)
                       | ((std::uint32_t) e.data[o + 3] << 24);
            };

            for (std::size_t o = 0; o + size <= e.data.size(); o += size)
            {
                ScoreNote n;
                n.start = rescale (u32 (o));
                n.length = rescale (u32 (o + 8));
                n.slide = (u16 (o + 4) & 0xF) == 8;
                int fine = 120, release = 64, extra = 0, pan = 64, velocity = 100, modX = 128, modY = 128;

                if (oldRecords)
                {
                    n.key = e.data[o + 12];
                    fine = e.data[o + 13];
                    extra = e.data[o + 15];
                    pan = e.data[o + 16];
                    velocity = e.data[o + 17];
                    modX = e.data[o + 18];
                    modY = e.data[o + 19];
                }
                else
                {
                    n.key = u16 (o + 12);
                    fine = e.data[o + 16];
                    release = e.data[o + 18];
                    extra = e.data[o + 19];
                    pan = e.data[o + 20];
                    velocity = e.data[o + 21];
                    modX = e.data[o + 22];
                    modY = e.data[o + 23];
                }

                if (version != 0 && version < 3001006u)
                    fine = 120;
                if (version != 0 && version < 6005000u)
                    release = 64;
                n.colour = version != 0 && version < 5003000u ? ((extra & 0x0F) << 2) & 0x0F : extra & 0x0F;
                n.muted = (extra & 0x20) != 0;
                n.porta = (extra & 0x10) != 0;
                n.group = oldRecords ? 0 : u16 (o + 14);
                n.velocity = velocity / 128.0f;
                n.pan = std::clamp ((pan - 64) / 64.0f, -1.0f, 1.0f);
                n.release = release / 128.0f;
                n.finePitch = (float) std::clamp (fine - 120, -120, 120);
                n.modX = modX / 255.0f;
                n.modY = modY / 255.0f;
                score.notes.push_back (n);
            }
        }

        if (score.notes.empty())
            return std::nullopt;

        std::stable_sort (score.notes.begin(), score.notes.end(), [] (auto& a, auto& b) { return a.start < b.start; });
        return score;
    }
    catch (const std::exception&)
    {
    }

    return std::nullopt;
}

std::vector<Note> notesFromScore (const Score& score, int channel)
{
    std::vector<Note> out;
    out.reserve (score.notes.size());
    for (const auto& s : score.notes)
    {
        Note n;
        n.start = std::max<std::int64_t> (0, s.start);
        n.length = std::max<std::int64_t> (1, s.length > 0 ? s.length : ticksPerStep);
        n.channel = channel;
        n.key = std::clamp (s.key, 0, 127);
        n.velocity = std::clamp (s.velocity, 0.0f, 1.0f);
        n.pan = std::clamp (s.pan, -1.0f, 1.0f);
        n.release = std::clamp (s.release, 0.0f, 1.0f);
        n.finePitch = std::clamp (s.finePitch, -120.0f, 120.0f);
        n.modX = std::clamp (s.modX, 0.0f, 1.0f);
        n.modY = std::clamp (s.modY, 0.0f, 1.0f);
        n.colour = std::clamp (s.colour, 0, noteColourCount - 1);
        n.slide = s.slide;
        n.muted = s.muted;
        n.porta = s.porta;
        n.group = s.group;
        out.push_back (n);
    }
    return out;
}

std::vector<std::uint8_t> writeScore (const std::vector<Note>& notes)
{
    std::vector<std::uint8_t> out;
    auto u8 = [&out] (int v) { out.push_back ((std::uint8_t) v); };
    auto u16 = [&u8] (int v) { u8 (v & 0xFF); u8 ((v >> 8) & 0xFF); };
    auto u32 = [&u16] (std::uint32_t v) { u16 ((int) (v & 0xFFFF)); u16 ((int) (v >> 16)); };
    auto byteOf = [] (double v, int lo, int hi) { return std::clamp ((int) std::lround (v), lo, hi); };

    // header: "FLhd", 6, format 0x10 (FL's score mode, the saver's param_2), one channel, the PPQ
    for (auto c : { 'F', 'L', 'h', 'd' })
        u8 (c);
    u32 (6);
    u16 (0x10);
    u16 (1);
    u16 (ppq);
    for (auto c : { 'F', 'L', 'd', 't' })
        u8 (c);
    const auto sizeAt = out.size();
    u32 (0);

    auto dataEvent = [&] (int id, const std::vector<std::uint8_t>& data) {
        u8 (id);
        auto size = data.size();
        do
        {
            auto b = (int) (size & 0x7F);
            size >>= 7;
            u8 (size != 0 ? b | 0x80 : b);
        } while (size != 0);
        out.insert (out.end(), data.begin(), data.end());
    };

    // the version text the 24-byte note records come with (parseScore reads it the way FL does)
    const std::string version = "20.9.0";
    dataEvent (0xC7, std::vector<std::uint8_t> (version.c_str(), version.c_str() + version.size() + 1));
    u8 (0x41); // the pattern (0)
    u16 (0);

    auto sorted = notes;
    std::stable_sort (sorted.begin(), sorted.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
    std::vector<std::uint8_t> records;
    records.reserve (sorted.size() * 24);
    for (const auto& n : sorted)
    {
        auto r32 = [&records] (std::uint32_t v) { for (int k = 0; k < 4; ++k) records.push_back ((std::uint8_t) (v >> (8 * k))); };
        auto r16 = [&records] (int v) { records.push_back ((std::uint8_t) (v & 0xFF)); records.push_back ((std::uint8_t) ((v >> 8) & 0xFF)); };
        r32 ((std::uint32_t) std::max<std::int64_t> (0, n.start));
        r16 (0x4000 | (n.slide ? 8 : 0)); // flags (0x4000 on every note FL writes; 8 = slide)
        r16 (0);                          // channel: FL sets it to 0 in a score (FUN_012dfdb0 (list, 0))
        r32 ((std::uint32_t) std::max<std::int64_t> (1, n.length));
        r16 (std::clamp (n.key, 0, 127));
        r16 (std::clamp (n.group, 0, 0xFFFF));
        records.push_back ((std::uint8_t) byteOf (120.0 + n.finePitch, 0, 240));
        records.push_back (0); // repeats
        records.push_back ((std::uint8_t) byteOf (n.release * 128.0, 0, 128));
        records.push_back ((std::uint8_t) ((std::clamp (n.colour, 0, 15)) | (n.porta ? 0x10 : 0) | (n.muted ? 0x20 : 0)));
        records.push_back ((std::uint8_t) byteOf (64.0 + n.pan * 64.0, 0, 128));
        records.push_back ((std::uint8_t) byteOf (n.velocity * 128.0, 0, 128));
        records.push_back ((std::uint8_t) byteOf (n.modX * 255.0, 0, 255));
        records.push_back ((std::uint8_t) byteOf (n.modY * 255.0, 0, 255));
    }
    dataEvent (0xE0, records);

    const auto size = (std::uint32_t) (out.size() - sizeAt - 4);
    for (int k = 0; k < 4; ++k)
        out[sizeAt + (std::size_t) k] = (std::uint8_t) (size >> (8 * k));
    return out;
}

std::vector<std::uint8_t> writeEventScore (const std::vector<PatternEvent>& events, std::uint32_t control)
{
    std::vector<std::uint8_t> out;
    auto u8 = [&out] (int v) { out.push_back ((std::uint8_t) v); };
    auto u16 = [&u8] (int v) { u8 (v & 0xFF); u8 ((v >> 8) & 0xFF); };
    auto u32 = [&u16] (std::uint32_t v) { u16 ((int) (v & 0xFFFF)); u16 ((int) (v >> 16)); };

    // header: "FLhd", 6, format 0x18 (FL's event score mode), one channel, the PPQ
    for (auto c : { 'F', 'L', 'h', 'd' })
        u8 (c);
    u32 (6);
    u16 (0x18);
    u16 (1);
    u16 (ppq);
    for (auto c : { 'F', 'L', 'd', 't' })
        u8 (c);
    const auto sizeAt = out.size();
    u32 (0);
    u8 (0x41); // the pattern (0)
    u16 (0);

    std::vector<std::uint8_t> records;
    records.reserve (events.size() * 12);
    auto r32 = [&records] (std::uint32_t v) { for (int k = 0; k < 4; ++k) records.push_back ((std::uint8_t) (v >> (8 * k))); };
    for (const auto& e : events)
    {
        r32 ((std::uint32_t) std::max<std::int64_t> (0, e.tick));
        r32 (control); // FL 2026's own Save automation as wrote 5 for Tempo (REC_Tempo's low bits); others not observed yet
        const auto v = std::clamp (e.value, 0.0f, 1.0f);
        std::uint32_t bits = 0;
        std::memcpy (&bits, &v, sizeof bits);
        r32 (bits); // the value normalised to 0..1, as a float (FUN_0105fc40)
    }
    u8 (0xDF);
    auto size = records.size();
    do
    {
        auto b = (int) (size & 0x7F);
        size >>= 7;
        u8 (size != 0 ? b | 0x80 : b);
    } while (size != 0);
    out.insert (out.end(), records.begin(), records.end());

    const auto total = (std::uint32_t) (out.size() - sizeAt - 4);
    for (int k = 0; k < 4; ++k)
        out[sizeAt + (std::size_t) k] = (std::uint8_t) (total >> (8 * k));
    return out;
}

std::optional<std::vector<PatternEvent>> parseEventScore (std::span<const std::uint8_t> bytes)
{
    try
    {
        flp::EventReader reader (bytes);
        if (reader.header().format != 0x18)
            return std::nullopt;
        const auto filePpq = std::max<int> (1, reader.header().ppq);
        flp::Event e;
        std::vector<PatternEvent> events;
        while (reader.next (e))
        {
            if (e.id != 0xDF || ! e.isData())
                continue;
            for (std::size_t o = 0; o + 12 <= e.data.size(); o += 12)
            {
                std::uint32_t tick = 0, bits = 0;
                std::memcpy (&tick, e.data.data() + o, 4);
                std::memcpy (&bits, e.data.data() + o + 8, 4);
                float v = 0.0f;
                std::memcpy (&v, &bits, sizeof v);
                if (! std::isfinite (v))
                    continue;
                events.push_back ({ roundEven ((double) tick * ppq / filePpq), std::clamp (v, 0.0f, 1.0f) });
            }
        }
        // sorted, one per tick (the last one of a tick wins, as FL applies them in order)
        std::stable_sort (events.begin(), events.end(), [] (const PatternEvent& a, const PatternEvent& b) { return a.tick < b.tick; });
        std::vector<PatternEvent> kept;
        for (const auto& ev : events)
            if (! kept.empty() && kept.back().tick == ev.tick)
                kept.back() = ev;
            else
                kept.push_back (ev);
        return kept;
    }
    catch (...)
    {
        return std::nullopt;
    }
}

Score gridScore (std::int64_t step, float swing)
{
    Score score;
    score.name = "Grid";
    step = std::clamp<std::int64_t> (step, 1, beatTicks);
    const auto count = std::max<std::int64_t> (1, beatTicks / step);

    for (std::int64_t i = 0; i < count; ++i)
    {
        ScoreNote n;
        n.start = i * step + ((i % 2) == 1 ? (std::int64_t) std::llround (std::clamp (swing, 0.0f, 1.0f) * (double) step * 0.5) : 0);
        n.length = step;
        score.notes.push_back (n);
    }

    return score;
}

// --- quantizer --------------------------------------------------------------------------------

namespace
{
struct GrooveMatch
{
    std::int64_t target = 0;
    const ScoreNote* note = nullptr;
};

// The nearest template point (start, or end when `end`) to `t` in the period before, at and after t's period,
// strictly inside `window`; FL's search (REA_rodada83 §1).
GrooveMatch nearestGroove (const Score& groove, std::int64_t period, std::int64_t t, std::int64_t window, bool end)
{
    GrooveMatch m;
    m.target = t;

    if (groove.notes.empty() || period <= 0)
        return m;

    auto base = (divTrunc (t, period) - 1) * period;
    auto best = window;

    for (int r = 0; r < 3; ++r, base += period)
        for (const auto& n : groove.notes)
        {
            auto candidate = base + n.start;
            if (end)
                candidate += n.length - 1;

            const auto distance = std::abs (t - candidate);

            if (distance < best)
            {
                best = distance;
                m.target = candidate;
                m.note = &n;
            }
        }

    return m;
}

std::int64_t moveToward (std::int64_t value, std::int64_t target, float strength)
{
    // FL: value + ((target - value) * s) / 128 with s = 0..128, rounded toward zero
    const auto s = (std::int64_t) std::llround (std::clamp (strength, 0.0f, 1.0f) * 128.0f);
    return value + divTrunc ((target - value) * s, 128);
}
} // namespace

void quantize (Pattern& pattern, const std::vector<int>& indices, const Score& groove, const QuantizeOptions& o)
{
    // FL rounds the template's period up to whole beats
    const auto period = std::max<std::int64_t> (1, (groove.length() + beatTicks - 1) / beatTicks) * beatTicks;
    const auto window = (std::int64_t) std::clamp (o.sensitivity, 12, 192) * ticksPerStep / 48;

    rewrite (pattern, indices, [&] (std::vector<Note>& notes) {
        for (auto& n : notes)
        {
            const auto end = n.start + n.length - 1;
            const auto m = nearestGroove (groove, period, n.start, window, false);
            n.start = std::max<std::int64_t> (0, moveToward (n.start, m.target, o.start));

            if (m.note != nullptr)
                blendLevels (n, *m.note, o.levels);

            if (o.durationMode != QuantizeOptions::leaveDuration && n.length > 0)
            {
                if (o.durationMode == QuantizeOptions::quantizeDuration)
                {
                    if (m.note != nullptr)
                        n.length = moveToward (n.length, m.note->length, o.duration);
                }
                else
                {
                    auto newEnd = end;

                    if (o.durationMode == QuantizeOptions::quantizeEnd)
                        newEnd = moveToward (end, nearestGroove (groove, period, end, window, true).target, o.duration);

                    n.length = std::max (minimumLength, newEnd - n.start + 1);
                }

                n.length = std::max<std::int64_t> (1, n.length);
            }
        }

        // FL drops the notes whose velocity became 0
        notes.erase (std::remove_if (notes.begin(), notes.end(), [] (const Note& n) { return n.velocity <= 0.0f; }), notes.end());
    });
}

// --- chopper ----------------------------------------------------------------------------------

float chopTimeMultiplier (int wheel) noexcept
{
    static const float table[] = { 0.125f, 0.25f, 0.5f, 1.0f, 2.0f, 3.0f, 4.0f };
    return table[std::clamp (wheel, 1, 7) - 1];
}

void chop (Pattern& pattern, const std::vector<int>& indices, const Score& source, const ChopOptions& o)
{
    // the pattern scaled by the time multiplier; a colour-12 note only sets the period
    const auto mul = chopTimeMultiplier (o.timeMul);
    std::vector<ScoreNote> pat;
    std::int64_t period = std::max<std::int64_t> (1, roundEven ((double) ((float) source.length() * mul)));

    for (auto n : source.notes)
    {
        n.start = roundEven ((double) ((float) n.start * mul));
        n.length = std::max<std::int64_t> (1, roundEven ((double) ((float) n.length * mul)));

        if (n.colour == 12)
        {
            period = n.length;
            continue;
        }

        pat.push_back (n);
    }

    std::stable_sort (pat.begin(), pat.end(), [] (auto& a, auto& b) { return a.start < b.start; });

    if (pat.empty())
        return;

    rewrite (pattern, indices, [&] (std::vector<Note>& notes) {
        std::vector<Note> out;

        for (const auto& n : notes)
        {
            if (n.length <= 0)
            {
                out.push_back (n);
                continue;
            }

            auto offset = o.absolute ? -floorMod (n.start, period) : 0;
            const auto end = n.start + n.length;
            bool firstRound = true;
            std::size_t k = 0;

            for (;;)
            {
                const auto& p = pat[k];
                auto pos = n.start + offset + p.start;

                if (pos >= end)
                    break;

                auto length = std::min (p.length, end - pos);

                if (p.colour == 4) // lasts to the note's end, only in the first round
                    length = firstRound ? end - pos : 0;

                if (pos < n.start)
                {
                    length -= n.start - pos;
                    pos = n.start;
                }

                if (length > 0)
                {
                    Note piece = n;
                    piece.start = pos;
                    piece.length = length;
                    piece.key = std::clamp (n.key + p.key - 60, 0, 127);
                    piece.slide = p.slide;
                    blendLevels (piece, p, o.levels);
                    out.push_back (piece);
                }

                if (++k == pat.size())
                {
                    k = 0;
                    offset += period;
                    firstRound = false;
                }
            }
        }

        notes = std::move (out);
    });
}

// --- flamer -----------------------------------------------------------------------------------

FlamOptions flamPreset (FlamPreset preset) noexcept
{
    switch (preset)
    {
        case FlamPreset::flam: return { -64, 85, true, false };
        case FlamPreset::earlyFlam: return { -64, 85, true, true };
        case FlamPreset::doubleStroke: return { 192, 85, false, false };
        case FlamPreset::earlyDoubleStroke: return { -192, 85, false, true };
    }
    return {};
}

std::int64_t flamOffset (const FlamOptions& o, double bpm) noexcept
{
    const auto time = std::clamp (o.time, -192, 192);

    if (! o.absolute)
        return divTrunc ((std::int64_t) time * ticksPerStep, 192);

    const auto ms = time * (100.0 / 192.0);
    return roundEven (ms * std::max (1.0, bpm) * ppq / 60000.0);
}

void flam (Pattern& pattern, const std::vector<int>& indices, const FlamOptions& o, double bpm)
{
    const auto t = flamOffset (o, bpm);
    const auto velocity = std::clamp (o.velocity, 0, 128) / 128.0f;

    rewrite (pattern, indices, [&] (std::vector<Note>& notes) {
        std::vector<Note> out;

        for (const auto& n : notes)
        {
            auto original = n.start;

            if (o.before != (t < 0))
                original = n.start - t;

            const auto extra = original + t;
            Note moved = n;
            moved.start = std::max<std::int64_t> (0, original);
            out.push_back (moved);

            if (std::max<std::int64_t> (0, extra) != original)
            {
                Note added = n;
                added.start = std::max<std::int64_t> (0, extra);
                added.velocity = std::clamp (n.velocity * velocity, 0.0f, 1.0f);

                if (added.start < original && original - added.start < added.length)
                    added.length = original - added.start;

                if (added.length > 0)
                    out.push_back (added);
            }
        }

        notes = std::move (out);
    });
}

// --- LFO --------------------------------------------------------------------------------------

double lfoPeriod (int speed) noexcept
{
    const auto exponent = 7.0 - std::clamp (speed, 0, 40960) / 4096.0;
    return std::max (1.0, (double) beatTicks * std::exp2 (exponent));
}

std::vector<float> lfoCurve (const LfoOptions& o, std::int64_t length)
{
    length = std::max<std::int64_t> (1, length);
    std::vector<float> out ((std::size_t) length);

    const double k = 1.0 / 65536.0;
    const auto startValue = (double) o.startValue, startRange = (double) o.startRange;
    const auto valueDelta = o.useEnd ? (double) (o.endValue - o.startValue) : 0.0;
    const auto rangeDelta = o.useEnd ? (double) (o.endRange - o.startRange) : 0.0;
    const auto endSpeed = o.useEnd ? o.endSpeed : o.startSpeed;

    // the period (or with log speed, the exponent) moves linearly from the start to the end value
    double speedNow = 0.0, speedStep = 0.0;

    if (o.logSpeed)
    {
        speedNow = 7.0 - std::clamp (o.startSpeed, 0, 40960) / 4096.0;
        speedStep = ((7.0 - std::clamp (endSpeed, 0, 40960) / 4096.0) - speedNow) / (double) length;
    }
    else
    {
        speedNow = lfoPeriod (o.startSpeed);
        speedStep = (lfoPeriod (endSpeed) - speedNow) / (double) length;
    }

    static const double shapeOffset[] = { 0.25, 0.75, 0.5 };
    const auto shape = std::clamp (o.shape, 0, 2);
    double phase = std::clamp (o.phase, 0, 65535) * k + shapeOffset[shape];

    for (std::int64_t t = 0; t < length; ++t)
    {
        phase -= std::floor (phase);
        double wave = 0.0;

        if (shape == LfoOptions::sine)
            wave = std::sin (phase * 6.283185307179586);
        else if (shape == LfoOptions::triangle)
            wave = 1.0 - std::abs (phase * 4.0 - 2.0);
        else
            wave = (double) (1 - 2 * roundEven (phase));

        const auto p = (double) t / (double) length;
        const auto v = (startValue + p * valueDelta) * k + wave * (startRange + p * rangeDelta) * k;
        out[(std::size_t) t] = (float) std::clamp (v, 0.0, 1.0);

        const auto period = o.logSpeed ? std::max (1.0, (double) beatTicks * std::exp2 (speedNow)) : speedNow;
        phase += 1.0 / period;
        speedNow += speedStep;
    }

    return out;
}

void lfo (Pattern& pattern, const std::vector<int>& indices, notetools::Level level, const LfoOptions& o)
{
    const auto valid = validSorted (pattern, indices);

    if (valid.empty())
        return;

    std::int64_t from = pattern.notes[(std::size_t) valid.front()].start, to = from + 1;

    for (auto i : valid)
    {
        const auto& n = pattern.notes[(std::size_t) i];
        from = std::min (from, n.start);
        to = std::max (to, n.start + std::max<std::int64_t> (1, n.length));
    }

    const auto curve = lfoCurve (o, to - from);

    for (auto i : valid)
    {
        auto& n = pattern.notes[(std::size_t) i];
        const auto t = std::clamp<std::int64_t> (n.start - from, 0, (std::int64_t) curve.size() - 1);
        notetools::setLevelValue (n, level, curve[(std::size_t) t]);
    }
}

// --- claw machine -----------------------------------------------------------------------------

const std::vector<const char*>& clawTrashNames()
{
    static const std::vector<const char*> names { "1/4", "2/4", "3/4", "4/4", "1/3", "2/3", "3/3", "1/2", "2/2", "(none)" };
    return names;
}

void claw (Pattern& pattern, const std::vector<int>& indices, const ClawOptions& o)
{
    const auto period = beatTicks << std::clamp (o.period, 0, 4);
    const auto trash = std::clamp (o.trash, 0, 9);
    // n slices per period; the trashed slice is rotated to the end of the period (k slices)
    const int slices = trash < 4 ? 4 : trash < 7 ? 3 : trash < 9 ? 2 : -1;
    static const int rotation[] = { 3, 2, 1, 0, 2, 1, 0, 1, 0, 0 };

    const auto slice = slices > 0 ? period / slices : period;
    const auto shift = slices > 0 ? rotation[trash] * slice : 0;
    const auto keep = period - slice;
    const auto scale = slices > 0 && o.stretch ? (double) slices / (double) (slices - 1) : 1.0;

    // time warp curve
    const auto d = std::clamp (o.timeDistortion, -1024, 1024) / 1024.0f;
    const auto c = (float) ((double) (float) (std::pow (101.0, (double) std::abs (d)) - 1.0) * (d < 0 ? -1.0 : d > 0 ? 1.0 : 0.0) * 0.5);
    const auto inv = c != 0.0f ? (float) (1.0 / std::abs ((double) c)) : 0.0f;
    const auto logc = c != 0.0f ? (float) std::log (std::abs ((double) c) + 1.0) : 0.0f;

    const auto warp = [&] (std::int64_t x) {
        const auto t = (double) x * scale;
        const auto base = (std::int64_t) std::floor (t / (double) period) * period;
        auto f = (t - (double) base) / (double) period;

        if (c > 0.0f)
            f = 1.0 - (std::exp ((1.0 - f) * (double) logc) - 1.0) * (double) inv;
        else if (c < 0.0f)
            f = (std::exp (f * (double) logc) - 1.0) * (double) inv;

        return roundEven ((double) base + f * (double) period);
    };

    rewrite (pattern, indices, [&] (std::vector<Note>& notes) {
        std::vector<Note> out;

        for (auto n : notes)
        {
            const auto originalLength = n.length;

            if (slices > 0)
            {
                n.start += shift;

                // FL's integer div/mod (truncating, as the lengths can go negative)
                if (n.start % period > keep) // starts in the trashed slice: move to the next period
                {
                    const auto skip = period - n.start % period;
                    n.start += skip;
                    n.length -= skip;
                }

                const auto endInPeriod = (n.start + n.length) % period;
                if (endInPeriod > keep)
                    n.length -= endInPeriod - keep;

                const auto start = n.start;
                n.start -= (start / period) * slice;
                n.length -= ((n.length + start % period) / period) * slice;
                n.start -= shift;
            }

            if (n.start < 0)
            {
                n.length += n.start;
                n.start = 0;
            }

            if (originalLength < 1)
            {
                n.start = warp (n.start);
                if (n.length >= 0)
                    out.push_back (n);
                continue;
            }

            const auto end = warp (n.start + n.length);
            n.start = warp (n.start);
            n.length = end - n.start;

            if (n.length < 1 || (o.removeShort && n.length < minimumLength))
                continue;

            n.length = std::max (n.length, minimumLength);
            out.push_back (n);
        }

        notes = std::move (out);
    });
}

// --- articulator ------------------------------------------------------------------------------

ArticulateOptions articulatePreset (ArticulatePreset preset) noexcept
{
    ArticulateOptions o;

    switch (preset)
    {
        case ArticulatePreset::legato: o.multiply = 100; break;
        case ArticulatePreset::portato: o.multiply = 75; break;
        case ArticulatePreset::staccato: o.multiply = 20; break;
        case ArticulatePreset::smallGaps: o.multiply = 100; o.gap = 20; o.useLengths = true; break;
        case ArticulatePreset::chopChordsOnly: o.multiply = 100; o.chopChords = true; o.useLengths = true; break;
    }

    return o;
}

void articulate (Pattern& pattern, const std::vector<int>& indices, const ArticulateOptions& o, Meter meter)
{
    const auto bar = barTicks (meter);
    const auto multiply = std::clamp (o.multiply, 10, 100) * 0.01f;
    const auto variation = std::clamp (o.variation, 0, 100) * 0.01f;
    const auto gap = roundEven ((double) std::clamp (o.gap, 0, 240) * ppq / 960.0);
    std::mt19937 random (o.seed);

    rewrite (pattern, indices, [&] (std::vector<Note>& notes) {
        if (o.chopChords) // cut held notes where a later note starts, continued by a copy
        {
            std::vector<Note> sorted = notes;
            std::int64_t last = -1;

            for (std::size_t i = 0; i < sorted.size(); ++i)
            {
                const auto s = sorted[i].start;
                if (s <= last)
                    continue;
                last = s;

                const auto count = sorted.size();
                for (std::size_t j = 0; j < count && sorted[j].start < s; ++j)
                {
                    auto& held = sorted[j];
                    if (held.start + held.length > s)
                    {
                        Note rest = held;
                        rest.start = s;
                        rest.length = held.start + held.length - s;
                        held.length = s - held.start;
                        sorted.push_back (rest);
                    }
                }

                std::stable_sort (sorted.begin() + (std::ptrdiff_t) i, sorted.end(),
                                  [] (const Note& a, const Note& b) { return a.start < b.start; });
            }

            notes = std::move (sorted);
        }

        const auto count = notes.size();
        std::vector<std::int64_t> lengths (count);

        for (std::size_t i = 0; i < count; ++i)
        {
            const auto& n = notes[i];
            std::int64_t room = 0;
            bool found = false;

            for (std::size_t j = i + 1; j < count; ++j)
                if (notes[j].start >= n.start + minimumLength)
                {
                    room = notes[j].start - n.start;
                    found = true;
                    break;
                }

            if (! found)
            {
                const auto end = n.start + n.length;
                const auto nearestBar = roundEven ((double) end / (double) bar) * bar;
                room = end == nearestBar ? n.length : (floorDiv (end + bar - 1, bar) * bar) - n.start;
            }

            const auto base = o.useLengths ? n.length : room;
            auto length = std::min ((float) base * multiply, (float) (room - gap));

            if (variation != 0.0f)
                length = (float) ((double) length - (double) random() * 2.3283064365386963e-10 * (double) (length * variation));

            lengths[i] = std::max (minimumLength, roundEven ((double) length));
        }

        for (std::size_t i = 0; i < count; ++i)
            notes[i].length = lengths[i];

        // the notes of the last chord take the last note's length
        if (! o.useLengths && count > 1)
            for (std::size_t i = 0; i + 1 < count; ++i)
                if (notes[i].start == notes[count - 1].start)
                    notes[i].length = lengths[count - 1];
    });
}

// --- chord progression ------------------------------------------------------------------------

namespace
{
const int majorSteps[] = { 0, 2, 4, 5, 7, 9, 11 };
const int minorSteps[] = { 0, 2, 3, 5, 7, 8, 10 };

int pitchClass (int v) noexcept
{
    return ((v % 12) + 12) % 12;
}

// "I".."VII" (either case) at the start of `s`; returns the degree 0..6 and how many characters it used
bool readNumeral (const std::string& s, std::size_t& pos, int& degree, bool& upper)
{
    static const char* names[] = { "VII", "III", "VI", "IV", "II", "V", "I" };
    static const int degrees[] = { 6, 2, 5, 3, 1, 4, 0 };

    for (int i = 0; i < 7; ++i)
    {
        const std::string n = names[i];
        if (pos + n.size() > s.size())
            continue;

        bool allUpper = true, allLower = true, match = true;

        for (std::size_t k = 0; k < n.size(); ++k)
        {
            const auto c = s[pos + k];
            if (std::toupper ((unsigned char) c) != n[k]) { match = false; break; }
            allUpper = allUpper && std::isupper ((unsigned char) c);
            allLower = allLower && std::islower ((unsigned char) c);
        }

        if (match && (allUpper || allLower))
        {
            degree = degrees[i];
            upper = allUpper;
            pos += n.size();
            return true;
        }
    }

    return false;
}

bool startsWith (const std::string& s, std::size_t pos, const char* what)
{
    const std::string w = what;
    return s.compare (pos, w.size(), w) == 0;
}

// accidentals: b, #, and the UTF-8 flat/sharp signs
int readAccidentals (const std::string& s, std::size_t& pos)
{
    int shift = 0;

    for (;;)
    {
        if (startsWith (s, pos, "\xE2\x99\xAD")) { shift--; pos += 3; }       // flat sign
        else if (startsWith (s, pos, "\xE2\x99\xAF")) { shift++; pos += 3; }  // sharp sign
        else if (pos < s.size() && s[pos] == 'b' && pos + 1 < s.size()
                 && std::string ("IViv").find (s[pos + 1]) != std::string::npos) { shift--; pos++; }
        else if (pos < s.size() && s[pos] == '#') { shift++; pos++; }
        else break;
    }

    return shift;
}

std::optional<Chord> parseChordSymbol (const std::string& s, int tonic, bool minor)
{
    std::size_t pos = 0;
    while (pos < s.size() && s[pos] == ' ')
        ++pos;

    const auto shift = readAccidentals (s, pos);
    int degree = 0;
    bool upper = true;

    if (! readNumeral (s, pos, degree, upper))
        return std::nullopt;

    // numerals are read against the major scale of the tonic ("bVI" = the flat sixth), also in minor keys
    (void) minor;
    const auto& steps = majorSteps;
    Chord c;
    c.root = pitchClass (tonic + steps[degree] + shift);
    std::vector<int> third { 0, upper ? 4 : 3, 7 };
    bool seventh = false, majorSeventh = false, dim = false, halfDim = false;
    std::vector<int> extra;
    int sus = 0;
    bool added = false;

    while (pos < s.size())
    {
        while (pos < s.size() && s[pos] == ' ')
            ++pos;
        if (pos >= s.size())
            break;

        if (s[pos] == '/')
        {
            ++pos;
            std::size_t p = pos;
            const auto bassShift = readAccidentals (s, p);
            int bassDegree = 0;
            bool bassUpper = true;

            if (! readNumeral (s, p, bassDegree, bassUpper))
                return std::nullopt;

            c.bass = pitchClass (tonic + steps[bassDegree] + bassShift);
            pos = p;
            continue;
        }

        if (startsWith (s, pos, "\xE2\x88\x86")) { majorSeventh = true; pos += 3; continue; } // increment sign (maj7)
        if (startsWith (s, pos, "\xC2\xB0")) { dim = true; pos += 2; continue; }              // degree sign
        if (startsWith (s, pos, "maj7")) { majorSeventh = true; pos += 4; continue; }
        if (startsWith (s, pos, "sus2")) { sus = 2; pos += 4; continue; }
        if (startsWith (s, pos, "sus4")) { sus = 4; pos += 4; continue; }
        if (startsWith (s, pos, "add")) { added = true; pos += 3; continue; }
        if (startsWith (s, pos, "dim")) { dim = true; pos += 3; continue; }
        if (startsWith (s, pos, "\xE2\x99\xAD" "5")) { halfDim = true; pos += 4; continue; } // flat 5
        if (startsWith (s, pos, "b5")) { halfDim = true; pos += 2; continue; }

        // extensions: "add9" adds only the ninth; a plain 9, 11 or 13 also implies the seventh
        int extension = 0;
        if (startsWith (s, pos, "13")) { extension = 21; pos += 2; }
        else if (startsWith (s, pos, "11")) { extension = 17; pos += 2; }
        else if (s[pos] == '9') { extension = 14; pos++; }
        else if (s[pos] == '7') { seventh = true; pos++; continue; }
        else if (s[pos] == '6') { extension = 9; pos++; }
        else if (s[pos] == '4') { extension = 5; pos++; }
        else if (s[pos] == '2') { extension = 2; pos++; }

        if (extension != 0)
        {
            if (! added && extension >= 14)
                seventh = true;
            extra.push_back (extension);
            added = false;
            continue;
        }

        return std::nullopt;
    }

    if (dim)
        third = { 0, 3, 6 };
    if (halfDim)
        third[2] = 6;
    if (sus == 2)
        third[1] = 2;
    else if (sus == 4)
        third[1] = 5;

    c.intervals = third;

    if (majorSeventh)
        c.intervals.push_back (11);
    else if (seventh)
        c.intervals.push_back (dim && ! halfDim ? 9 : 10);

    for (auto e : extra)
        c.intervals.push_back (e);

    std::sort (c.intervals.begin(), c.intervals.end());
    c.intervals.erase (std::unique (c.intervals.begin(), c.intervals.end()), c.intervals.end());
    return c;
}
} // namespace

std::optional<std::vector<Chord>> parseProgression (const std::string& text, int tonic, bool minor)
{
    std::vector<Chord> out;
    std::string part;

    const auto flush = [&]() -> bool {
        auto trimmed = part;
        while (! trimmed.empty() && trimmed.back() == ' ')
            trimmed.pop_back();
        while (! trimmed.empty() && trimmed.front() == ' ')
            trimmed.erase (trimmed.begin());
        part.clear();

        if (trimmed.empty())
            return true;

        const auto chord = parseChordSymbol (trimmed, tonic, minor);
        if (! chord)
            return false;
        out.push_back (*chord);
        return true;
    };

    // chords are separated by '-' or ','; a space separates them too unless what follows is a quality ("ii 7",
    // "I ∆", "vi /bVI")
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        const auto c = text[i];

        if (c == ':')
            break;

        if (c == '-' || c == ',')
        {
            if (! flush())
                return std::nullopt;
            continue;
        }

        if (c == ' ')
        {
            std::size_t p = i + 1;
            while (p < text.size() && text[p] == ' ')
                ++p;
            std::size_t q = p;
            readAccidentals (text, q);
            int d = 0;
            bool up = false;
            std::size_t r = q;

            if (p < text.size() && text[p] != '/' && readNumeral (text, r, d, up) && ! part.empty())
            {
                if (! flush())
                    return std::nullopt;
                continue;
            }
        }

        part += c;
    }

    if (! flush() || out.empty())
        return std::nullopt;

    return out;
}

std::string chordName (const Chord& c)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    std::string s = names[pitchClass (c.root)];
    const auto has = [&c] (int i) { return std::find (c.intervals.begin(), c.intervals.end(), i) != c.intervals.end(); };

    if (has (3) && has (6) && ! has (7))
        s += has (10) ? "m7b5" : has (9) ? "dim7" : "dim";
    else
    {
        if (has (3))
            s += "m";
        if (has (11))
            s += "maj7";
        else if (has (10))
            s += "7";
        if (has (2) && ! has (3) && ! has (4))
            s += "sus2";
        if (has (5) && ! has (3) && ! has (4))
            s += "sus4";
        if (has (14))
            s += has (10) || has (11) ? "(9)" : "add9";
    }

    if (c.bass >= 0 && c.bass != c.root)
        s += std::string ("/") + names[pitchClass (c.bass)];

    return s;
}

std::optional<DetectedChord> detectChord (std::vector<int> keys, bool preferFlats)
{
    keys.erase (std::remove_if (keys.begin(), keys.end(), [] (int k) { return k < 0 || k > 127; }), keys.end());
    std::sort (keys.begin(), keys.end());
    keys.erase (std::unique (keys.begin(), keys.end()), keys.end());
    if (keys.size() > 10)
        keys.resize (10);

    unsigned set = 0;
    for (auto k : keys)
        set |= 1u << (k % 12);
    if (std::popcount (set) < 2)
        return std::nullopt;

    struct Template { std::initializer_list<int> intervals; const char* suffix; };
    // in order of preference when one set fits several roots equally
    static const Template templates[] = {
        { { 0, 4, 7 }, "" },            { { 0, 3, 7 }, "m" },          { { 0, 3, 6 }, "dim" },
        { { 0, 4, 8 }, "aug" },         { { 0, 2, 7 }, "sus2" },       { { 0, 5, 7 }, "sus4" },
        { { 0, 4, 7, 10 }, "7" },       { { 0, 4, 7, 11 }, "maj7" },   { { 0, 3, 7, 10 }, "m7" },
        { { 0, 3, 6, 10 }, "m7b5" },    { { 0, 3, 6, 9 }, "dim7" },    { { 0, 3, 7, 11 }, "m(maj7)" },
        { { 0, 4, 7, 9 }, "6" },        { { 0, 3, 7, 9 }, "m6" },      { { 0, 5, 7, 10 }, "7sus4" },
        { { 0, 2, 4, 7 }, "add9" },     { { 0, 2, 3, 7 }, "madd9" },   { { 0, 4, 8, 10 }, "7#5" },
        { { 0, 4, 6, 10 }, "7b5" },     { { 0, 4, 8, 11 }, "maj7#5" },
        { { 0, 2, 4, 7, 10 }, "9" },    { { 0, 2, 4, 7, 11 }, "maj9" }, { { 0, 2, 3, 7, 10 }, "m9" },
        { { 0, 1, 4, 7, 10 }, "7b9" },  { { 0, 3, 4, 7, 10 }, "7#9" }, { { 0, 2, 4, 7, 9 }, "6/9" },
        { { 0, 2, 5, 7, 10 }, "9sus4" }, { { 0, 2, 3, 5, 7, 10 }, "m11" }, { { 0, 4, 7, 9, 10 }, "13" },
        { { 0, 4, 10 }, "7" },          { { 0, 4, 11 }, "maj7" },      { { 0, 3, 10 }, "m7" }, // without the fifth
        { { 0, 7 }, "5" },
    };
    static const char* sharps[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    static const char* flats[] = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
    const auto names = preferFlats ? flats : sharps;
    const auto bass = keys.front() % 12;

    auto mask = [] (const Template& t, int root) {
        unsigned m = 0;
        for (auto i : t.intervals)
            m |= 1u << ((root + i) % 12);
        return m;
    };

    // the bass as the root first, then any template on another root (a slash chord)
    for (const auto& t : templates)
        if (mask (t, bass) == set)
            return DetectedChord { bass, bass, std::string (names[bass]) + t.suffix };

    for (const auto& t : templates)
        for (int root = 0; root < 12; ++root)
            if (root != bass && (set >> root & 1u) != 0 && mask (t, root) == set)
                return DetectedChord { root, bass, std::string (names[root]) + t.suffix + "/" + names[bass] };

    return std::nullopt;
}

std::string toNumerals (const std::vector<Chord>& chords, int tonic)
{
    static const char* upperNames[] = { "I", "II", "III", "IV", "V", "VI", "VII" };
    // pitch class above the tonic -> degree of the major scale and accidental
    static const int degreeOf[12] = { 0, 1, 1, 2, 2, 3, 4, 4, 5, 5, 6, 6 };
    static const int flatOf[12] = { 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0 };
    std::string out;

    for (const auto& c : chords)
    {
        if (! out.empty())
            out += "-";

        const auto has = [&c] (int i) { return std::find (c.intervals.begin(), c.intervals.end(), i) != c.intervals.end(); };
        const auto rel = pitchClass (c.root - tonic);
        std::string n = upperNames[degreeOf[rel]];

        if (flatOf[rel])
            n = "b" + n;

        const auto minorThird = has (3) && ! has (4);
        if (minorThird)
            for (auto& ch : n)
                if (ch != 'b')
                    ch = (char) std::tolower ((unsigned char) ch);

        if (has (3) && has (6) && ! has (7))
            n += has (10) ? "7b5" : "dim";
        else
        {
            if (has (2) && ! has (3) && ! has (4))
                n += "sus2";
            if (has (5) && ! has (3) && ! has (4))
                n += "sus4";
            if (has (11))
                n += "maj7";
            else if (has (10))
                n += "7";
        }

        if (has (14))
            n += "add9";

        if (c.bass >= 0 && c.bass != c.root)
        {
            const auto b = pitchClass (c.bass - tonic);
            n += std::string ("/") + (flatOf[b] ? "b" : "") + upperNames[degreeOf[b]];
        }

        out += n;
    }

    return out;
}

std::vector<ProgressionPreset> parseProgressionPresets (const std::string& text)
{
    std::vector<ProgressionPreset> out;
    bool minor = false;
    std::size_t start = 0;

    while (start <= text.size())
    {
        auto end = text.find ('\n', start);
        if (end == std::string::npos)
            end = text.size();

        auto line = text.substr (start, end - start);
        start = end + 1;

        while (! line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.pop_back();

        if (line.empty())
            continue;

        if (line[0] == '-')
        {
            minor = line.find ("inor") != std::string::npos;
            continue;
        }

        ProgressionPreset p;
        p.name = line;
        p.numerals = line.substr (0, line.find (':'));
        p.minor = minor;

        if (parseProgression (p.numerals, 0, minor))
            out.push_back (p);
    }

    return out;
}

const std::vector<ProgressionPreset>& builtInProgressions()
{
    // common, well-known diatonic progressions
    static const std::vector<ProgressionPreset> list {
        { "I-V-vi-IV", "I-V-vi-IV", false },   { "I-vi-IV-V", "I-vi-IV-V", false }, { "vi-IV-I-V", "vi-IV-I-V", false },
        { "I-IV-V-IV", "I-IV-V-IV", false },   { "ii7-V7-Imaj7", "ii7-V7-Imaj7", false }, { "I-IV-vi-V", "I-IV-vi-V", false },
        { "i-VI-III-VII", "i-VI-III-VII", true }, { "i-iv-v-i", "i-iv-v-i", true }, { "i-VII-VI-VII", "i-VII-VI-VII", true },
        { "i-iv-VII-III", "i-iv-VII-III", true },
    };
    return list;
}

std::vector<Chord> generateProgression (int tonic, bool minor, int count, float adventure, std::uint32_t seed)
{
    // LYNX's own table of moves between scale degrees (0..6): the usual functional pulls, with weights; more
    // "adventurous" settings flatten the weights and allow sevenths and borrowed chords.
    static const float moves[7][7] = {
        // to:  I    ii   iii  IV   V    vi   vii
        { 0.2f, 0.6f, 0.4f, 1.0f, 1.0f, 0.9f, 0.1f }, // from I
        { 0.3f, 0.1f, 0.2f, 0.4f, 1.0f, 0.3f, 0.3f }, // ii
        { 0.3f, 0.2f, 0.1f, 0.7f, 0.3f, 1.0f, 0.1f }, // iii
        { 0.9f, 0.5f, 0.2f, 0.1f, 1.0f, 0.4f, 0.2f }, // IV
        { 1.0f, 0.2f, 0.3f, 0.5f, 0.1f, 0.8f, 0.1f }, // V
        { 0.4f, 0.7f, 0.4f, 1.0f, 0.7f, 0.1f, 0.1f }, // vi
        { 1.0f, 0.1f, 0.5f, 0.1f, 0.2f, 0.3f, 0.0f }, // vii
    };

    count = std::clamp (count, 1, 64);
    adventure = std::clamp (adventure, 0.0f, 1.0f);
    std::mt19937 random (seed);
    std::uniform_real_distribution<float> uniform (0.0f, 1.0f);

    const auto& steps = minor ? minorSteps : majorSteps;
    static const bool majorUpper[] = { true, false, false, true, true, false, false };
    static const bool minorUpper[] = { false, false, true, false, false, true, true };

    std::vector<Chord> out;
    int degree = 0;

    for (int i = 0; i < count; ++i)
    {
        if (i > 0)
        {
            float weights[7], total = 0.0f;

            for (int d = 0; d < 7; ++d)
            {
                weights[d] = moves[degree][d] * (1.0f - adventure) + adventure * 0.5f;
                if (i == count - 1 && count > 2) // end on a chord that leads home
                    weights[d] *= (d == 4 || d == 3) ? 2.0f : 1.0f;
                total += weights[d];
            }

            auto pick = uniform (random) * total;
            int next = 0;

            for (; next < 6; ++next)
            {
                pick -= weights[next];
                if (pick <= 0.0f)
                    break;
            }

            degree = next;
        }

        Chord c;
        c.root = pitchClass (tonic + steps[degree]);
        const auto upper = minor ? minorUpper[degree] : majorUpper[degree];
        const auto diminished = (! minor && degree == 6) || (minor && degree == 1);
        c.intervals = diminished ? std::vector<int> { 0, 3, 6 } : std::vector<int> { 0, upper ? 4 : 3, 7 };

        if (minor && degree == 4 && uniform (random) < 0.5f * (1.0f - adventure)) // the harmonic minor's major V
            c.intervals = { 0, 4, 7 };

        if (uniform (random) < adventure * 0.6f) // sevenths (diatonic)
        {
            const auto seventh = pitchClass (steps[(degree + 6) % 7] - steps[degree]);
            c.intervals.push_back (seventh == 0 ? 11 : seventh);
        }

        if (uniform (random) < adventure * 0.25f) // a borrowed / altered colour
            c.intervals.push_back (14);

        out.push_back (c);
    }

    return out;
}

std::vector<RhythmPreset> parseRhythmPresets (const std::string& text)
{
    std::vector<RhythmPreset> out;
    std::size_t start = 0;

    while (start < text.size())
    {
        auto end = text.find ('\n', start);
        if (end == std::string::npos)
            end = text.size();

        auto line = text.substr (start, end - start);
        start = end + 1;

        while (! line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.pop_back();

        std::vector<std::string> fields;
        std::size_t p = 0;

        while (p <= line.size())
        {
            auto q = line.find (',', p);
            if (q == std::string::npos)
                q = line.size();
            fields.push_back (line.substr (p, q - p));
            p = q + 1;
        }

        if (fields.size() < 3 || fields[2] == "r") // random rhythms (FL's generator) are not supported
            continue;

        RhythmPreset r;
        r.name = fields[0];

        for (std::size_t i = 2; i < fields.size(); ++i)
            if (fields[i].size() == 1)
                r.cells += fields[i][0];

        if (! r.cells.empty())
            out.push_back (r);
    }

    return out;
}

std::vector<int> writeProgression (Pattern& pattern, int channel, std::int64_t start, const std::vector<Chord>& chords,
                                   const ChordWriteOptions& o)
{
    std::vector<Note> added;
    const auto length = std::max<std::int64_t> (ticksPerStep, o.chordLength);
    const auto baseC = 12 * (std::clamp (o.octave, 1, 8) + 1); // C of the octave (C4 = 60)
    std::vector<int> previous;

    for (std::size_t i = 0; i < chords.size(); ++i)
    {
        const auto& c = chords[i];
        std::vector<int> keys;

        for (auto iv : c.intervals)
            keys.push_back (baseC + c.root + iv);

        // voice leading: try inversions near the previous chord's centre
        if (o.voiceLead && ! previous.empty() && keys.size() >= 3)
        {
            const auto centre = [] (const std::vector<int>& k) { double s = 0; for (auto v : k) s += v; return s / (double) k.size(); };
            const auto target = centre (previous);
            auto best = keys;

            for (int shift = -(int) keys.size(); shift <= (int) keys.size(); ++shift)
            {
                auto candidate = keys;
                std::sort (candidate.begin(), candidate.end());

                for (int s = 0; s < std::abs (shift); ++s)
                {
                    if (shift > 0) { candidate.front() += 12; std::sort (candidate.begin(), candidate.end()); }
                    else { candidate.back() -= 12; std::sort (candidate.begin(), candidate.end()); }
                }

                if (std::abs (centre (candidate) - target) < std::abs (centre (best) - target))
                    best = candidate;
            }

            keys = best;
        }

        previous = keys;
        const auto chordStart = start + (std::int64_t) i * length;

        // rhythm: cells split 4 x chord length; '>' starts a hit, '-' holds it, anything else rests
        std::vector<std::pair<std::int64_t, std::int64_t>> hits;

        if (o.rhythm.empty())
            hits.push_back ({ chordStart, length });
        else
        {
            const auto cells = (std::int64_t) o.rhythm.size();
            const auto block = 4 * length;
            const auto cell = std::max<std::int64_t> (1, block / cells);
            const auto blockStart = start + ((std::int64_t) i / 4) * block;

            for (std::int64_t k = 0; k < cells; ++k)
            {
                const auto t = blockStart + k * cell;
                if (t < chordStart || t >= chordStart + length)
                    continue;

                if (o.rhythm[(std::size_t) k] == '>' || o.rhythm[(std::size_t) k] == 'p')
                    hits.push_back ({ t, cell });
                else if (o.rhythm[(std::size_t) k] == '-' && ! hits.empty() && hits.back().first + hits.back().second == t)
                    hits.back().second += cell;
            }
        }

        for (const auto& [t, len] : hits)
        {
            for (auto k : keys)
            {
                Note n;
                n.start = t;
                n.length = len;
                n.channel = channel;
                n.key = std::clamp (k, 0, 127);
                n.velocity = o.velocity;
                added.push_back (n);
            }

            if (o.bassNote)
            {
                Note n;
                n.start = t;
                n.length = len;
                n.channel = channel;
                n.key = std::clamp (baseC - 12 + (c.bass >= 0 ? c.bass : c.root), 0, 127);
                n.velocity = o.velocity;
                added.push_back (n);
            }
        }
    }

    const auto first = (int) pattern.notes.size();
    pattern.notes.insert (pattern.notes.end(), added.begin(), added.end());

    // find the new notes again after sorting
    std::vector<int> result;
    std::vector<bool> isNew (pattern.notes.size(), false);
    for (auto i = (std::size_t) first; i < pattern.notes.size(); ++i)
        isNew[i] = true;

    std::vector<std::pair<Note, bool>> tagged;
    for (std::size_t i = 0; i < pattern.notes.size(); ++i)
        tagged.push_back ({ pattern.notes[i], isNew[i] });
    std::stable_sort (tagged.begin(), tagged.end(), [] (auto& a, auto& b) { return a.first.start < b.first.start; });

    for (std::size_t i = 0; i < tagged.size(); ++i)
    {
        pattern.notes[i] = tagged[i].first;
        if (tagged[i].second)
            result.push_back ((int) i);
    }

    return result;
}

// --- riff machine -----------------------------------------------------------------------------

namespace
{
ScoreNote scoreNote (std::int64_t start, std::int64_t length, int key, float velocity = 100.0f / 128.0f)
{
    ScoreNote n;
    n.start = start;
    n.length = length;
    n.key = key;
    n.velocity = velocity;
    return n;
}

// LYNX's own riff shapes: (step, length in steps, key offset from 60) over one bar
struct Shape
{
    const char* name;
    std::vector<std::array<int, 3>> notes;
};

const std::vector<Shape>& progressionShapes()
{
    static const std::vector<Shape> shapes {
        { TR_NOOP ("Pulse"), { { 0, 2, 0 }, { 2, 2, 0 }, { 4, 2, 7 }, { 6, 2, 0 }, { 8, 2, 3 }, { 10, 2, 0 }, { 12, 2, 7 }, { 14, 2, 5 } } },
        { TR_NOOP ("Climb"), { { 0, 3, 0 }, { 3, 3, 2 }, { 6, 2, 3 }, { 8, 3, 5 }, { 11, 3, 7 }, { 14, 2, 10 } } },
        { TR_NOOP ("Offbeat"), { { 2, 2, 0 }, { 6, 2, 0 }, { 10, 2, 7 }, { 14, 2, 5 } } },
        { TR_NOOP ("Gallop"), { { 0, 1, 0 }, { 1, 1, 0 }, { 2, 2, 12 }, { 4, 1, 0 }, { 5, 1, 0 }, { 6, 2, 10 }, { 8, 1, 0 }, { 9, 1, 0 }, { 10, 2, 7 }, { 12, 4, 5 } } },
        { TR_NOOP ("Long"), { { 0, 6, 0 }, { 6, 2, 3 }, { 8, 6, 5 }, { 14, 2, 7 } } },
        { TR_NOOP ("Syncopated"), { { 0, 3, 0 }, { 3, 3, 0 }, { 6, 2, 12 }, { 8, 3, 7 }, { 11, 3, 5 }, { 14, 2, 3 } } },
    };
    return shapes;
}

const std::vector<Shape>& chordShapes()
{
    static const std::vector<Shape> shapes {
        { TR_NOOP ("Single"), { { 0, 16, 0 } } },
        { TR_NOOP ("Fifth"), { { 0, 16, 0 }, { 0, 16, 7 } } },
        { TR_NOOP ("Minor triad"), { { 0, 16, 0 }, { 0, 16, 3 }, { 0, 16, 7 } } },
        { TR_NOOP ("Major triad"), { { 0, 16, 0 }, { 0, 16, 4 }, { 0, 16, 7 } } },
        { TR_NOOP ("Octaves"), { { 0, 16, 0 }, { 0, 16, 12 } } },
        { TR_NOOP ("Minor 7th"), { { 0, 16, 0 }, { 0, 16, 3 }, { 0, 16, 7 }, { 0, 16, 10 } } },
    };
    return shapes;
}

Score shapeScore (const Shape& s, bool fullLength)
{
    Score score;
    score.name = s.name;

    for (const auto& n : s.notes)
        score.notes.push_back (scoreNote ((std::int64_t) n[0] * ticksPerStep, (std::int64_t) n[1] * ticksPerStep, 60 + n[2]));

    if (fullLength) // chord shapes: hold to the note end (FL's colour 4 behaviour)
        for (auto& n : score.notes)
            n.colour = 4;

    return score;
}
} // namespace

Score builtInRiffProgression (int variant)
{
    const auto& s = progressionShapes();
    return shapeScore (s[(std::size_t) (((variant % (int) s.size()) + (int) s.size()) % (int) s.size())], false);
}

Score builtInRiffChord (int variant)
{
    const auto& s = chordShapes();
    return shapeScore (s[(std::size_t) (((variant % (int) s.size()) + (int) s.size()) % (int) s.size())], true);
}

int builtInRiffProgressionCount() noexcept { return (int) progressionShapes().size(); }
int builtInRiffChordCount() noexcept { return (int) chordShapes().size(); }

std::vector<int> riffMachine (Pattern& pattern, const std::vector<int>& indices, const RiffOptions& o, Meter meter)
{
    // work on a separate pattern holding only the riff's notes
    Pattern work;
    const auto valid = validSorted (pattern, indices);

    if (o.onExistingNotes)
        for (auto i : valid)
            work.notes.push_back (pattern.notes[(std::size_t) i]);

    for (auto it = valid.rbegin(); it != valid.rend(); ++it)
        pattern.notes.erase (pattern.notes.begin() + *it);

    if (! o.onExistingNotes)
    {
        Note base;
        base.start = o.start;
        base.length = (std::int64_t) std::clamp (o.bars, 1, 64) * barTicks (meter);
        base.channel = o.channel;
        base.key = 48;
        base.velocity = 100.0f / 128.0f;
        work.notes.push_back (base);
    }

    work.sortNotes();
    const auto all = [&work] {
        std::vector<int> v ((std::size_t) work.notes.size());
        for (std::size_t i = 0; i < v.size(); ++i)
            v[i] = (int) i;
        return v;
    };

    const auto last = std::clamp (o.stepsUpTo, 0, 7);

    for (int step = 0; step <= last; ++step)
    {
        if (! o.enabled[step] || work.notes.empty())
            continue;

        switch (step)
        {
            case 0: chop (work, all(), o.progression.notes.empty() ? builtInRiffProgression (0) : o.progression, o.progressionChop); break;
            case 1: chop (work, all(), o.chord.notes.empty() ? builtInRiffChord (0) : o.chord, o.chordChop); break;
            case 2: notetools::arpeggiate (work, all(), o.arp); break;
            case 3: notetools::randomise (work, all(), o.levels); break;
            case 4: notetools::flip (work, all(), o.flipTime, o.flipPitch); break;
            case 5: articulate (work, all(), o.articulation, meter); break;
            case 6: quantize (work, all(), o.groove.notes.empty() ? gridScore (ticksPerStep) : o.groove, o.grooveOptions); break;
            case 7:
                notetools::limit (work, all(), o.fit);
                if (o.fitScale > 0)
                    notetools::snapNotesToScale (work, all(), o.fitRoot, o.fitScale);
                break;
            default: break;
        }

        work.sortNotes();
    }

    const auto first = pattern.notes.size();

    for (auto n : work.notes)
    {
        n.channel = o.channel;
        pattern.notes.push_back (n);
    }

    // indices of the riff's notes after sorting
    std::vector<std::pair<Note, bool>> tagged;
    for (std::size_t i = 0; i < pattern.notes.size(); ++i)
        tagged.push_back ({ pattern.notes[i], i >= first });
    std::stable_sort (tagged.begin(), tagged.end(), [] (auto& a, auto& b) { return a.first.start < b.first.start; });

    std::vector<int> result;
    for (std::size_t i = 0; i < tagged.size(); ++i)
    {
        pattern.notes[i] = tagged[i].first;
        if (tagged[i].second)
            result.push_back ((int) i);
    }

    return result;
}
} // namespace lynx::prtools
