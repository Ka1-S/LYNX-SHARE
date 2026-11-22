// Round 190nw (LYNX 0.1.31): the built-in tracker, stage 1 — the editor model (core/TrackerGrid.h: a view of the
// patterns' own notes, lossless both ways with the Piano roll) and the module import (core/ModuleImport.h: .mod, .xm,
// .s3m and .it through libxmp-lite). The modules are written byte by byte here (no third-party song in the repo). The
// .lynx round trip and the copy for 0.1.30 / 0.1.28 are in the app's --selftest ("tracker190nw").

#include "core/FlpExport.h"
#include "core/KeyMap.h"
#include "core/ModuleImport.h"
#include "core/SessionIds.h"
#include "core/TempoMarkers.h"
#include "core/TrackerGrid.h"

namespace tracker190nw
{
using namespace lynx;

inline Note note (int channel, int key, std::int64_t start, std::int64_t length)
{
    Note n;
    n.channel = channel;
    n.key = key;
    n.start = start;
    n.length = length;
    return n;
}

inline Song song (int channels = 2)
{
    Song s;
    s.mixer = Song::defaultMixer (4);
    for (int c = 0; c < channels; ++c)
        s.channels.emplace_back().name = "Ch " + std::to_string (c + 1);
    s.patterns.resize (1);
    s.patterns[0].name = "P";
    s.patterns[0].lengthTicks = 4 * ppq * 4; // 4 bars
    return s;
}

inline tracker::Layout layoutOf (const Song& s, int rpb = 4, std::vector<int> alsoShow = {})
{
    tracker::BuildOptions o;
    o.rowsPerBeat = rpb;
    o.alsoShow = std::move (alsoShow);
    return tracker::build (s, 0, o);
}

// every note placed once, its start and end exact from the rows and delays, no two notes on one cell of a column
inline bool lossless (const Song& s, const tracker::Layout& l)
{
    const auto& notes = s.patterns[0].notes;
    std::vector<int> seen (notes.size(), 0);
    for (const auto& p : l.placed)
    {
        if (p.note < 0 || p.note >= (int) notes.size())
            return false;
        ++seen[(std::size_t) p.note];
        const auto& n = notes[(std::size_t) p.note];
        if (p.startRow * l.rowTicks + p.startDelay != n.start)
            return false;
        if (p.offRow * l.rowTicks + p.offDelay != n.start + n.length)
            return false;
        if (p.shortNote != (p.offRow == p.startRow))
            return false;
    }
    for (auto v : seen)
        if (v != 1)
            return false;
    // one cell per row and column: a start, an OFF or a sustain, never two
    for (int t = 0; t < (int) l.tracks.size(); ++t)
        for (int c = 0; c < l.tracks[(std::size_t) t].noteColumns; ++c)
        {
            std::map<std::int64_t, int> used;
            for (auto pi : l.columns[(std::size_t) t][(std::size_t) c])
            {
                const auto& p = l.placed[(std::size_t) pi];
                if (used[p.startRow]++ > 0)
                    return false;
                const auto last = p.shortNote ? p.startRow : (p.offShown ? p.offRow : p.occupiedEnd - 1);
                for (auto r = p.startRow + 1; r <= last && r < l.rows; ++r)
                    if (l.placedAt (t, c, r) == nullptr && used[r]++ > 0)
                        return false;
            }
        }
    return true;
}

// ------------------------------------------------------------------------------------------------ module writers
inline void put16le (std::vector<std::uint8_t>& b, int v) { b.push_back ((std::uint8_t) v); b.push_back ((std::uint8_t) (v >> 8)); }
inline void put32le (std::vector<std::uint8_t>& b, std::uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back ((std::uint8_t) (v >> (8 * i))); }
inline void putText (std::vector<std::uint8_t>& b, const std::string& s, std::size_t size)
{
    for (std::size_t i = 0; i < size; ++i)
        b.push_back (i < s.size() ? (std::uint8_t) s[i] : 0);
}
inline std::vector<std::int8_t> wave (int frames)
{
    std::vector<std::int8_t> w ((std::size_t) frames);
    for (int i = 0; i < frames; ++i)
        w[(std::size_t) i] = (std::int8_t) std::lround (100.0 * std::sin (2.0 * 3.14159265358979 * i / frames));
    return w;
}

// ProTracker M.K.: 4 channels, sample 1 (64 frames, volume 64), orders 0 1. Pattern 0: row 0 C-2 + F03 (speed 3),
// row 4 E-2, row 6 (channel 3) C-2 + arpeggio 047, row 8 set volume 20 (no note), row 16 (channel 4) pattern break.
// Pattern 1: row 0 (channel 2) D-2 + EC2 (cut on tick 2), row 8 (channel 2) C-2 + ED1 (delay one tick).
inline std::vector<std::uint8_t> modFile()
{
    std::vector<std::uint8_t> b;
    putText (b, "lynx mod", 20);
    for (int s = 0; s < 31; ++s)
    {
        putText (b, s == 0 ? "sine" : "", 22);
        const auto words = s == 0 ? 32 : 0;
        b.push_back ((std::uint8_t) (words >> 8));
        b.push_back ((std::uint8_t) words);
        b.push_back (0);                   // finetune
        b.push_back (s == 0 ? 64 : 0);     // volume
        b.push_back (0), b.push_back (0);  // loop start
        b.push_back (0), b.push_back (1);  // loop length (1 = none)
    }
    b.push_back (2);   // song length
    b.push_back (127);
    for (int o = 0; o < 128; ++o)
        b.push_back (o < 2 ? (std::uint8_t) o : 0);
    putText (b, "M.K.", 4);
    std::vector<std::uint8_t> patterns (2 * 64 * 4 * 4, 0);
    auto cell = [&] (int pattern, int row, int channel, int sample, int period, int fx, int param) {
        auto* c = &patterns[(std::size_t) (((pattern * 64 + row) * 4 + channel) * 4)];
        c[0] = (std::uint8_t) ((sample & 0xF0) | ((period >> 8) & 0x0F));
        c[1] = (std::uint8_t) period;
        c[2] = (std::uint8_t) (((sample & 15) << 4) | fx);
        c[3] = (std::uint8_t) param;
    };
    cell (0, 0, 0, 1, 428, 0xF, 0x03);
    cell (0, 4, 0, 1, 339, 0, 0);
    cell (0, 6, 2, 1, 428, 0x0, 0x47);
    cell (0, 8, 0, 0, 0, 0xC, 0x20);
    cell (0, 16, 3, 0, 0, 0xD, 0x00);
    cell (1, 0, 1, 1, 381, 0xE, 0xC2);
    cell (1, 8, 1, 1, 428, 0xE, 0xD1);
    b.insert (b.end(), patterns.begin(), patterns.end());
    for (auto v : wave (64))
        b.push_back ((std::uint8_t) v);
    return b;
}

// FastTracker 2: 2 channels, one 8-row pattern, linear frequencies, speed 6 / tempo 125. Instrument 1 has two samples:
// keys up to B-3 play sample 1, from C-4 on sample 2 (relative note +12). Row 0: C-4 (volume column 32) on channel 1,
// C-5 with 8FF (pan right) on channel 2; row 2: key off on channel 1.
inline std::vector<std::uint8_t> xmFile()
{
    std::vector<std::uint8_t> b;
    putText (b, "Extended Module: ", 17);
    putText (b, "lynx xm", 20);
    b.push_back (0x1A);
    putText (b, "LYNX test", 20);
    put16le (b, 0x0104);
    put32le (b, 276);
    put16le (b, 1);   // song length
    put16le (b, 0);   // restart
    put16le (b, 2);   // channels
    put16le (b, 1);   // patterns
    put16le (b, 1);   // instruments
    put16le (b, 1);   // linear
    put16le (b, 6);   // speed
    put16le (b, 125); // tempo
    for (int o = 0; o < 256; ++o)
        b.push_back (0);
    // pattern 0
    put32le (b, 9);
    b.push_back (0);
    put16le (b, 8);
    put16le (b, 8 * 2 * 5);
    std::vector<std::uint8_t> cells (8 * 2 * 5, 0);
    auto cell = [&] (int row, int channel, int noteValue, int ins, int vol, int fx, int param) {
        auto* c = &cells[(std::size_t) ((row * 2 + channel) * 5)];
        c[0] = (std::uint8_t) noteValue, c[1] = (std::uint8_t) ins, c[2] = (std::uint8_t) vol, c[3] = (std::uint8_t) fx, c[4] = (std::uint8_t) param;
    };
    cell (0, 0, 49, 1, 0x10 + 32, 0, 0);
    cell (0, 1, 61, 1, 0, 0x8, 0xFF);
    cell (2, 0, 97, 0, 0, 0, 0);
    b.insert (b.end(), cells.begin(), cells.end());
    // instrument 1
    const auto start = b.size();
    put32le (b, 263);
    putText (b, "two zones", 22);
    b.push_back (0);
    put16le (b, 2);
    put32le (b, 40);
    for (int k = 0; k < 96; ++k)
        b.push_back (k < 48 ? 0 : 1);
    for (int i = 0; i < 96; ++i) // envelopes
        b.push_back (0);
    for (int i = 0; i < 16; ++i) // points, sustain / loop, types, vibrato
        b.push_back (0);
    put16le (b, 0); // fadeout
    while (b.size() < start + 263)
        b.push_back (0);
    for (int s = 0; s < 2; ++s)
    {
        put32le (b, 32);  // length
        put32le (b, 0);
        put32le (b, 0);
        b.push_back (64); // volume
        b.push_back (0);  // finetune
        b.push_back (0);  // type
        b.push_back (128);
        b.push_back (s == 0 ? 0 : 12); // relative note
        b.push_back (0);
        putText (b, s == 0 ? "low" : "high", 22);
    }
    for (int s = 0; s < 2; ++s)
    {
        std::int8_t last = 0;
        for (auto v : wave (32))
        {
            b.push_back ((std::uint8_t) (std::int8_t) (v - last));
            last = v;
        }
    }
    return b;
}

// Scream Tracker 3: channels L1 and R1, one instrument (c2spd 8363, unsigned 8-bit), orders 0 then the end. Row 0:
// C-4 on channel 1, A-4 + A03 (speed 3) on channel 2; row 2: note cut on channel 1.
inline std::vector<std::uint8_t> s3mFile()
{
    std::vector<std::uint8_t> b;
    putText (b, "lynx s3m", 28);
    b.push_back (0x1A);
    b.push_back (16);
    b.push_back (0), b.push_back (0);
    put16le (b, 2); // orders
    put16le (b, 1); // instruments
    put16le (b, 1); // patterns
    put16le (b, 0);
    put16le (b, 0x1320);
    put16le (b, 2); // unsigned samples
    putText (b, "SCRM", 4);
    b.push_back (64), b.push_back (6), b.push_back (125), b.push_back (0xB0), b.push_back (0), b.push_back (0);
    for (int i = 0; i < 8; ++i)
        b.push_back (0);
    put16le (b, 0);
    for (int c = 0; c < 32; ++c)
        b.push_back (c == 0 ? 0 : c == 1 ? 8 : 255);
    b.push_back (0), b.push_back (255); // orders
    // parapointers: the instrument at 0x70 (= 7 · 16), the pattern at 0xC0, the sample data at 0x200
    put16le (b, 0x70 / 16);
    put16le (b, 0xC0 / 16);
    while (b.size() < 0x70)
        b.push_back (0);
    b.push_back (1);
    putText (b, "sine.raw", 12);
    b.push_back (0); // memseg (high byte)
    put16le (b, 0x200 / 16);
    put32le (b, 64);
    put32le (b, 0);
    put32le (b, 0);
    b.push_back (64), b.push_back (0), b.push_back (0), b.push_back (0);
    put32le (b, 8363);
    for (int i = 0; i < 12; ++i)
        b.push_back (0);
    putText (b, "sine", 28);
    putText (b, "SCRS", 4);
    while (b.size() < 0xC0)
        b.push_back (0);
    std::vector<std::uint8_t> p;
    p.push_back (0x20 | 0), p.push_back (0x40), p.push_back (1);
    p.push_back (0x20 | 0x80 | 1), p.push_back (0x49), p.push_back (1), p.push_back (1), p.push_back (3);
    p.push_back (0);           // end of row 0
    p.push_back (0);           // row 1
    p.push_back (0x20 | 0), p.push_back (254), p.push_back (0);
    p.push_back (0);           // end of row 2
    for (int r = 3; r < 64; ++r)
        p.push_back (0);
    put16le (b, (int) p.size() + 2);
    b.insert (b.end(), p.begin(), p.end());
    while (b.size() < 0x200)
        b.push_back (0);
    for (auto v : wave (64))
        b.push_back ((std::uint8_t) (v + 128));
    return b;
}

// Impulse Tracker, sample mode: speed 6, tempo 150; one sample at c5speed 16726 (an octave above 8363). Row 0: C-5 on
// channel 1, C-5 + SD3 (delay 3 ticks) on channel 2; row 2: note cut on channel 1, note off on channel 2.
inline std::vector<std::uint8_t> itFile()
{
    std::vector<std::uint8_t> b;
    putText (b, "IMPM", 4);
    putText (b, "lynx it", 26);
    put16le (b, 0x0410);
    put16le (b, 2); // orders
    put16le (b, 0); // instruments
    put16le (b, 1); // samples
    put16le (b, 1); // patterns
    put16le (b, 0x0214);
    put16le (b, 0x0214);
    put16le (b, 1 | 8); // stereo, linear slides
    put16le (b, 0);
    b.push_back (128), b.push_back (48), b.push_back (6), b.push_back (150), b.push_back (128), b.push_back (0);
    put16le (b, 0);
    put32le (b, 0);
    put32le (b, 0);
    for (int c = 0; c < 64; ++c)
        b.push_back (c == 0 ? 0 : c == 1 ? 64 : 128 + 32);
    for (int c = 0; c < 64; ++c)
        b.push_back (64);
    b.push_back (0), b.push_back (255); // orders
    const auto sampleAt = 0xC0 + 2 + 4 + 4;
    const auto patternAt = sampleAt + 0x50;
    put32le (b, (std::uint32_t) sampleAt);
    put32le (b, (std::uint32_t) patternAt);
    std::vector<std::uint8_t> p;
    p.push_back (0x81), p.push_back (1 | 2), p.push_back (60), p.push_back (1);
    p.push_back (0x82), p.push_back (1 | 2 | 8), p.push_back (60), p.push_back (1), p.push_back (19), p.push_back (0xD3);
    p.push_back (0);
    p.push_back (0);
    p.push_back (0x81), p.push_back (1), p.push_back (254);
    p.push_back (0x82), p.push_back (1), p.push_back (255);
    p.push_back (0);
    for (int r = 3; r < 8; ++r)
        p.push_back (0);
    const auto dataAt = patternAt + 8 + (int) p.size();
    // sample header
    putText (b, "IMPS", 4);
    putText (b, "sine.raw", 12);
    b.push_back (0), b.push_back (64), b.push_back (1), b.push_back (64);
    putText (b, "sine", 26);
    b.push_back (1), b.push_back (0);
    put32le (b, 64);
    put32le (b, 0);
    put32le (b, 0);
    put32le (b, 16726);
    put32le (b, 0);
    put32le (b, 0);
    put32le (b, (std::uint32_t) dataAt);
    b.push_back (0), b.push_back (0), b.push_back (0), b.push_back (0);
    // pattern
    put16le (b, (int) p.size());
    put16le (b, 8);
    put32le (b, 0);
    b.insert (b.end(), p.begin(), p.end());
    for (auto v : wave (64))
        b.push_back ((std::uint8_t) v);
    return b;
}

struct Imported
{
    std::optional<Song> song;
    FlpImportReport report;
    ModuleImportSummary summary;
    std::vector<std::pair<std::string, std::vector<std::uint8_t>>> samples;
    std::string error;
};

inline Imported importOf (const std::vector<std::uint8_t>& bytes, bool volumeAsPatternEvents = true) // (round 190om: false = 190ob's clips)
{
    Imported out;
    ModuleImportOptions o;
    o.volumeAsPatternEvents = volumeAsPatternEvents;
    o.report = &out.report;
    o.summary = &out.summary;
    o.saveSample = [&out] (const std::vector<std::uint8_t>& wav, const std::string& name) {
        out.samples.emplace_back (name, wav);
        return "/tmp/lynx-190nw/" + name;
    };
    out.song = importModule (bytes, o, &out.error);
    return out;
}

inline const Note* noteAt (const Song& s, int pattern, std::int64_t start, int key = -1)
{
    for (const auto& n : s.patterns[(std::size_t) pattern].notes)
        if (n.start == start && (key < 0 || n.key == key))
            return &n;
    return nullptr;
}

inline std::uint32_t wavRate (const std::vector<std::uint8_t>& wav)
{
    return wav.size() > 28 ? (std::uint32_t) wav[24] | (std::uint32_t) wav[25] << 8 | (std::uint32_t) wav[26] << 16 | (std::uint32_t) wav[27] << 24 : 0;
}
} // namespace tracker190nw

// ===================================================================================================== the editor model
TEST (tracker190nw_layout_is_lossless_for_random_patterns)
{
    using namespace tracker190nw;
    std::mt19937 rng (190);
    for (const auto rpb : { 1, 3, 4, 6, 16 })
        for (int round = 0; round < 40; ++round)
        {
            auto s = song (3);
            auto& notes = s.patterns[0].notes;
            const auto count = 1 + (int) (rng() % 40);
            for (int i = 0; i < count; ++i)
            {
                // on and off the grid, chords, legato chains, tiny notes, overlaps
                auto start = (std::int64_t) (rng() % (16 * ppq));
                if (rng() % 2 == 0)
                    start -= start % ticksPerStep;
                auto length = (std::int64_t) (1 + rng() % (2 * ppq));
                if (rng() % 4 == 0)
                    length = 1 + rng() % 30;
                auto n = note ((int) (rng() % 3), 36 + (int) (rng() % 48), start, length);
                n.muted = rng() % 7 == 0;
                n.chance = rng() % 5 == 0 ? 0.25f : 1.0f;
                notes.push_back (n);
                if (rng() % 5 == 0) // a legato follower
                    notes.push_back (note (n.channel, n.key + 2, start + length, 1 + (std::int64_t) (rng() % ppq)));
            }
            s.patterns[0].sortNotes();
            const auto before = s.patterns[0];
            const auto l = layoutOf (s, rpb);
            CHECK (lossless (s, l));
            CHECK (s.patterns[0] == before); // a view: building it changes nothing
        }
}

TEST (tracker190nw_columns_for_chords_legato_and_short_notes)
{
    using namespace tracker190nw;
    auto s = song (1);
    auto& n = s.patterns[0].notes;
    // a chord of three on row 0 (one beat long), a legato pair, a note shorter than a row
    n = { note (0, 60, 0, ppq), note (0, 64, 0, ppq), note (0, 67, 0, ppq), note (0, 62, 2 * ppq, ppq / 2),
          note (0, 64, 2 * ppq + ppq / 2, ppq / 2), note (0, 70, 4 * ppq + 10, 50) };
    s.patterns[0].sortNotes();
    const auto l = layoutOf (s);
    CHECK (l.tracks.size() == 1 && l.tracks[0].noteColumns == 3);
    CHECK (l.rowTicks == 240 && l.rowsPerBar == 16);
    // the legato pair shares column 0, the first one's OFF hidden by the second's start
    const auto* a = l.placedAt (0, 0, 8);
    const auto* b = l.placedAt (0, 0, 10);
    CHECK (a != nullptr && b != nullptr && ! a->offShown);
    CHECK (tracker::cellAt (l, 0, 0, 9).kind == tracker::Cell::Kind::sustain);
    // the chord: OFFs on row 4 in all three columns
    for (int c = 0; c < 3; ++c)
        CHECK (tracker::cellAt (l, 0, c, 4).kind == tracker::Cell::Kind::off);
    // the short note: Cxx = its length, its delay 10
    const auto* sh = l.placedAt (0, 0, 16);
    CHECK (sh != nullptr && sh->shortNote && sh->startDelay == 10);
    if (sh != nullptr)
    {
        const auto fx = tracker::shownFx (l, *sh, s.patterns[0].notes[(std::size_t) sh->note]);
        CHECK (fx.size() == 1 && fx[0] == tracker::Fx::cut);
        CHECK (tracker::fieldValue (s, l, *sh, tracker::Field::fxValue, 0) == 50);
        CHECK (tracker::fieldValue (s, l, *sh, tracker::Field::dly, 0) == 10);
    }
    CHECK (lossless (s, l));
    // a preferred column keeps a note where the user typed it
    tracker::BuildOptions o;
    o.preferredColumn = [] (const Note& x) { return x.key == 62 ? 2 : -1; };
    const auto l2 = tracker::build (s, 0, o);
    CHECK (l2.placedAt (0, 2, 8) != nullptr);
    CHECK (lossless (s, l2));
}

TEST (tracker190nw_key_entry_notes_offs_and_values)
{
    using namespace tracker190nw;
    auto s = song (2);
    auto& p = s.patterns[0];
    auto l = layoutOf (s, 4, { 0 });
    CHECK (l.tracks.size() == 1 && l.rows == 64);
    // Z at octave 4 plays C4 (MIDI 48), as FL's typing keyboard
    const auto key = keys::typingPianoNote ('Z', 4);
    CHECK (key == 48);
    tracker::Cursor c;
    c.row = 0;
    // a note on an empty column lasts to the pattern's end
    CHECK (tracker::typeNote (p, l, c, key, 0, 0.78f) == 0);
    CHECK (p.notes.size() == 1 && p.notes[0].length == 64 * 240);
    l = layoutOf (s, 4, { 0 });
    // a note four rows down ends the first one there
    c.row = 4;
    tracker::typeNote (p, l, c, 52, 0, 0.5f);
    CHECK (p.notes.size() == 2 && p.notes[0].length == 4 * 240 && p.notes[1].start == 960 && p.notes[1].length == 60 * 240);
    l = layoutOf (s, 4, { 0 });
    // a note typed between them lasts to the next one (legato), and the one before ends at it
    c.row = 2;
    tracker::typeNote (p, l, c, 50, 0, 0.5f);
    CHECK (p.notes.size() == 3 && p.notes[0].length == 480 && p.notes[1].start == 480 && p.notes[1].length == 480);
    l = layoutOf (s, 4, { 0 });
    CHECK (l.tracks[0].noteColumns == 1);
    // OFF on row 8: the second note (row 4) ends there
    c.row = 8;
    CHECK (tracker::typeOff (p, l, c));
    CHECK (p.notes[2].length == 4 * 240);
    l = layoutOf (s, 4, { 0 });
    CHECK (tracker::cellAt (l, 0, 0, 8).kind == tracker::Cell::Kind::off);
    // the OFF's Dly: the end moves inside the row
    c.field = tracker::Field::dly;
    CHECK (tracker::setField (p, l, c, 0x30));
    CHECK (p.notes[2].length == 4 * 240 + 0x30);
    l = layoutOf (s, 4, { 0 });
    // Del on the OFF's note field: the note lasts to the pattern's end again
    c.field = tracker::Field::note;
    CHECK (tracker::clearCell (p, l, c));
    CHECK (p.notes[2].start + p.notes[2].length == 64 * 240);
    l = layoutOf (s, 4, { 0 });
    // values on the note of row 2: Vol 40, Pan 7F, Dly 10, Fx Y80 then its value, the rest of the note untouched
    p.notes[1].modX = 0.3f;
    p.notes[1].colour = 5;
    c.row = 2;
    c.field = tracker::Field::vol;
    CHECK (tracker::setField (p, l, c, 0x40) && std::abs (p.notes[1].velocity - 64.0f / 127.0f) < 1e-6f);
    c.field = tracker::Field::pan;
    CHECK (tracker::setField (p, l, c, 0x80) && p.notes[1].pan == 1.0f);
    c.field = tracker::Field::fxLetter;
    CHECK (tracker::setFxLetter (p, l, c, tracker::Fx::chance) && std::abs (p.notes[1].chance - 128.0f / 255.0f) < 1e-6f);
    l = layoutOf (s, 4, { 0 });
    c.field = tracker::Field::fxValue;
    CHECK (tracker::setField (p, l, c, 0x40) && std::abs (p.notes[1].chance - 64.0f / 255.0f) < 1e-6f);
    const auto end = p.notes[1].start + p.notes[1].length;
    c.field = tracker::Field::dly;
    CHECK (tracker::setField (p, l, c, 0x10));
    const auto* moved = noteAt (s, 0, 480 + 0x10);
    CHECK (moved != nullptr && moved->start + moved->length == end && moved->modX == 0.3f && moved->colour == 5);
    // Del on Vol: back to 0.78
    l = layoutOf (s, 4, { 0 });
    c.field = tracker::Field::vol;
    CHECK (tracker::clearCell (p, l, c));
    CHECK (noteAt (s, 0, 480 + 0x10) != nullptr && noteAt (s, 0, 480 + 0x10)->velocity == 0.78f);
    // a key typed on a note's start cell changes only the key
    l = layoutOf (s, 4, { 0 });
    c.field = tracker::Field::note;
    const auto before = *noteAt (s, 0, 480 + 0x10);
    tracker::typeNote (p, l, c, 70, 1, 1.0f);
    const auto after = *noteAt (s, 0, 480 + 0x10);
    CHECK (after.key == 70 && after.channel == before.channel && after.velocity == before.velocity && after.chance == before.chance);
}

TEST (tracker190nw_value_entry_and_rows)
{
    using namespace tracker190nw;
    tracker::ValueEntry e;
    bool done = false;
    CHECK (e.add (tracker::digitOf ('7', true), true, 2, done) == 7 && ! done);
    CHECK (e.add (tracker::digitOf ('F', true), true, 2, done) == 0x7F && done);
    e.reset();
    CHECK (e.add (1, false, 3, done) == 1 && e.add (2, false, 3, done) == 12 && e.add (7, false, 3, done) == 127 && done);
    CHECK (tracker::digitOf ('G', true) == -1 && tracker::digitOf ('a', true) == 10 && tracker::digitOf ('a', false) == -1);
    CHECK (tracker::numberText (0x7F, 2, true) == "7F" && tracker::numberText (127, 3, false) == "127" && tracker::numberText (-1, 2, true) == "..");
    CHECK (tracker::keyText (60) == "C-5" && tracker::keyText (61) == "C#5");
    // Insert / Backspace on one track: the other track stays
    auto s = song (2);
    auto& p = s.patterns[0];
    p.notes = { note (0, 60, 0, 480), note (0, 62, 480, 480), note (1, 64, 480, 240) };
    auto l = layoutOf (s);
    CHECK (tracker::insertRow (p, l, 0, 1)); // the first note crosses row 1: a row longer; the second moves down
    CHECK (p.notes[0].length == 720 && noteAt (s, 0, 720, 62) != nullptr && noteAt (s, 0, 480, 64) != nullptr);
    l = layoutOf (s);
    CHECK (tracker::deleteRow (p, l, 0, 1));
    CHECK (noteAt (s, 0, 0, 60)->length == 480 && noteAt (s, 0, 480, 62) != nullptr && noteAt (s, 0, 480, 64) != nullptr);
    l = layoutOf (s);
    CHECK (tracker::deleteRow (p, l, 0, 2)); // the row of the second note: it goes
    CHECK (noteAt (s, 0, 480, 62) == nullptr && p.notes.size() == 2);
    // the playhead's row
    CHECK (tracker::rowAt (l, 0.0) == 0 && tracker::rowAt (l, 479.0) == 1 && tracker::rowAt (l, -1.0) == -1);
}

TEST (tracker190nw_selection_copy_paste_transpose_interpolate)
{
    using namespace tracker190nw;
    auto s = song (2);
    auto& p = s.patterns[0];
    auto a = note (0, 60, 0, 240);
    a.modX = 0.1f, a.modY = 0.9f, a.colour = 3, a.release = 0.2f, a.finePitch = 12.0f, a.slide = true;
    p.notes = { a, note (0, 62, 240, 240), note (0, 64, 480, 240), note (1, 40, 0, 960) };
    p.notes[1].velocity = 0.2f;
    p.notes[2].velocity = 1.0f;
    auto l = layoutOf (s);
    tracker::Cursor from, to;
    from.row = 0, to.row = 3, to.track = 0;
    const auto sel = tracker::Selection::of (from, to);
    CHECK (tracker::selectedNotes (l, sel).size() == 3);
    const auto clip = tracker::copy (p, l, sel);
    CHECK (clip.notes.size() == 3 && clip.rows == 4);
    // paste at row 8: lossless copies (every field), on the same channel
    tracker::Cursor at;
    at.row = 8;
    const auto pasted = tracker::paste (p, l, at, clip);
    CHECK (pasted.size() == 3);
    const auto* copyA = noteAt (s, 0, 8 * 240, 60);
    CHECK (copyA != nullptr);
    if (copyA != nullptr)
    {
        auto expect = a;
        expect.start = 8 * 240;
        CHECK (*copyA == expect);
    }
    // the copy on the other track takes that track's channel
    l = layoutOf (s);
    at.track = 1;
    at.row = 16;
    tracker::paste (p, l, at, clip);
    CHECK (noteAt (s, 0, 16 * 240, 60) != nullptr && noteAt (s, 0, 16 * 240, 60)->channel == 1);
    // transpose the first block up an octave, then interpolate its volumes
    l = layoutOf (s);
    CHECK (tracker::transpose (p, l, sel, 12));
    CHECK (noteAt (s, 0, 0, 72) != nullptr && noteAt (s, 0, 240, 74) != nullptr && noteAt (s, 0, 0, 40) != nullptr);
    l = layoutOf (s);
    p.notes[0].velocity = 0.0f;
    CHECK (tracker::interpolateVolume (p, l, sel));
    CHECK (std::abs (noteAt (s, 0, 240, 74)->velocity - 0.5f) < 1e-5f);
    // erase
    l = layoutOf (s);
    CHECK (tracker::erase (p, l, sel) && noteAt (s, 0, 0, 72) == nullptr && noteAt (s, 0, 8 * 240, 60) != nullptr);
}

TEST (tracker190nw_text_clipboard_round_trip)
{
    using namespace tracker190nw;
    auto s = song (1);
    auto& p = s.patterns[0];
    auto a = note (0, 61, 10, 470);
    a.velocity = 100.0f / 127.0f;
    a.chance = 0x40 / 255.0f;
    p.notes = { a, note (0, 66, 960, 1200), note (0, 67, 960 + 240 * 6, 37) };
    const auto l = layoutOf (s);
    tracker::Cursor from, to;
    to.row = 15;
    const auto text = tracker::clipboardText (s, l, tracker::Selection::of (from, to), true);
    CHECK (text.rfind ("LYNX tracker 1", 0) == 0);
    CHECK (text.find ("C#5 001 64 40 0A Y40") != std::string::npos);
    const auto clip = tracker::parseClipboardText (text, l.rowTicks);
    CHECK (clip.has_value());
    if (! clip)
        return;
    CHECK (clip->notes.size() == 3 && clip->rows == 16);
    auto t = song (1);
    auto lt = layoutOf (t, 4, { 0 });
    tracker::paste (t.patterns[0], lt, {}, *clip);
    CHECK (t.patterns[0].notes.size() == 3);
    for (std::size_t i = 0; i < 3 && i < t.patterns[0].notes.size(); ++i)
    {
        const auto& x = p.notes[i];
        const auto& y = t.patterns[0].notes[i];
        CHECK (x.key == y.key && x.start == y.start && x.length == y.length);
        CHECK (std::lround (x.velocity * 127) == std::lround (y.velocity * 127));
        CHECK (std::lround (x.chance * 255) == std::lround (y.chance * 255));
    }
    CHECK (! tracker::parseClipboardText ("hello", 240).has_value());
}

TEST (tracker190nw_groups_and_the_ins_column)
{
    using namespace tracker190nw;
    auto s = song (3);
    uids::normalize (s);
    s.tracker.groups.push_back ({ "Drums", { s.channels[0].uid, s.channels[2].uid } });
    auto& p = s.patterns[0];
    p.notes = { note (0, 36, 0, 240), note (2, 38, 240, 240), note (1, 60, 0, 960) };
    tracker::BuildOptions o;
    tracker::resolveGroups (s, s.tracker, o);
    CHECK (o.groups.size() == 1 && o.groups[0] == std::vector<int> ({ 0, 2 }));
    const auto l = tracker::build (s, 0, o);
    CHECK (l.tracks.size() == 2 && l.tracks[0].group && l.tracks[0].name == "Drums" && l.tracks[1].channels == std::vector<int> ({ 1 }));
    CHECK (l.tracks[0].noteColumns == 1);
    tracker::ShownColumns shown;
    const auto fields = tracker::fieldsOf (l, 0, shown);
    CHECK (fields.size() >= 2 && fields[1].field == tracker::Field::ins);
    const auto* snare = l.placedAt (0, 0, 1);
    CHECK (snare != nullptr && tracker::fieldValue (s, l, *snare, tracker::Field::ins, 0) == 3);
    // Ins 1 moves the note to channel 1 (the group's first)
    tracker::Cursor c;
    c.row = 1;
    c.field = tracker::Field::ins;
    CHECK (tracker::setField (p, l, c, 1, 3) && noteAt (s, 0, 240, 38)->channel == 0);
    // the default settings are not saved (TrackerSettings::isDefault)
    CHECK (TrackerSettings {}.isDefault() && ! s.tracker.isDefault());
}

TEST (tracker190nw_tracker_edits_survive_the_flp_export)
{
    using namespace tracker190nw;
    auto s = song (1);
    s.channels[0].instrument = InstrumentKind::synth;
    auto& p = s.patterns[0];
    auto l = layoutOf (s, 4, { 0 });
    tracker::Cursor c;
    for (int i = 0; i < 6; ++i)
    {
        c.row = i * 3;
        tracker::typeNote (p, l, c, 48 + i, 0, 0.5f + 0.05f * i);
        l = layoutOf (s, 4, { 0 });
    }
    c.row = 17;
    tracker::typeOff (p, l, c);
    Clip clip;
    clip.pattern = 0;
    clip.length = p.effectiveLength (s.meter());
    s.playlist.push_back (clip);
    flpexport::Options options;
    flpexport::Report report;
    const auto flp = flpexport::exportFlp (s, options, report);
    FlpImportOptions io;
    io.fileExists = [] (const std::string&) { return false; };
    const auto back = importFlp (flp::parseProject (flp), io);
    CHECK (! back.patterns.empty());
    if (back.patterns.empty())
        return;
    const auto& got = back.patterns[0].notes;
    CHECK (got.size() == p.notes.size());
    for (std::size_t i = 0; i < got.size() && i < p.notes.size(); ++i)
        CHECK (got[i].key == p.notes[i].key && got[i].start == p.notes[i].start && got[i].length == p.notes[i].length
               && std::abs (got[i].velocity - p.notes[i].velocity) < 0.01f);
}

// ===================================================================================================== the import
TEST (tracker190nw_import_mod)
{
    using namespace tracker190nw;
    const auto in = importOf (modFile());
    CHECK (in.song.has_value());
    if (! in.song)
    {
        std::printf ("  (error: %s)\n", in.error.c_str());
        return;
    }
    const auto& s = *in.song;
    CHECK (in.summary.format.find ("Protracker") != std::string::npos || in.summary.format.find ("M.K.") != std::string::npos);
    CHECK (s.channels.size() == 1 && s.channels[0].instrument == InstrumentKind::sampler && s.channels[0].zones.size() == 1);
    CHECK (in.samples.size() == 1 && wavRate (in.samples[0].second) == 8287); // PAL, as libxmp plays a ProTracker module
    CHECK (s.channels[0].zones[0].rootKey == 60 && s.channels[0].zones[0].tune == 0.0f);
    // speed 3, tempo 125: 250 BPM at 4 rows a beat
    CHECK (s.tracker.rowsPerBeat == 4 && std::abs (s.bpm - 250.0) < 1e-6);
    // the order list: pattern 0 to its break (rows 0-16), then pattern 1
    CHECK (s.playlist.size() >= 2);
    const auto clips = std::count_if (s.playlist.begin(), s.playlist.end(), [] (const Clip& c) { return c.kind == ClipKind::pattern; });
    CHECK (clips == 2);
    CHECK (s.playlist[0].start == 0 && s.playlist[0].length == 17 * 240 && s.playlist[1].start == 17 * 240 && s.playlist[1].length == 64 * 240);
    const auto p0 = s.playlist[0].pattern, p1 = s.playlist[1].pattern;
    // pattern 0: C-2 (key 60) to the next note on row 4; E-2 (key 64) to the clip's end (held over: counted)
    const auto* c2 = noteAt (s, p0, 0, 60);
    const auto* e2 = noteAt (s, p0, 960, 64);
    CHECK (c2 != nullptr && c2->length == 960 && std::abs (c2->velocity - 1.0f) < 1e-6f && c2->pan < 0.0f);
    CHECK (e2 != nullptr && e2->length == 17 * 240 - 960);
    CHECK (in.summary.heldOverClipEnd >= 2);
    // pattern 1: D-2 cut on tick 2 of 3 (160 ticks); C-2 delayed one tick (80 ticks) on channel 2 (right)
    const auto* d2 = noteAt (s, p1, 0, 62);
    CHECK (d2 != nullptr && d2->length == 160 && d2->pan > 0.0f);
    CHECK (noteAt (s, p1, 8 * 240 + 80, 60) != nullptr);
    // the arpeggio: not imported in round 190nw; since round 190om the note's pitch curve (Tracker190omTests)
    CHECK (in.report.converted.count ("Module effect as the note's pitch curve: arpeggio (0xy)") == 1);
    CHECK (s.patterns[(std::size_t) p0].lengthTicks == 64 * 240);
}

TEST (tracker190nw_import_xm)
{
    using namespace tracker190nw;
    const auto in = importOf (xmFile());
    CHECK (in.song.has_value());
    if (! in.song)
    {
        std::printf ("  (error: %s)\n", in.error.c_str());
        return;
    }
    const auto& s = *in.song;
    CHECK (s.channels.size() == 1 && s.channels[0].zones.size() >= 2 && in.samples.size() == 2);
    if (s.channels.size() != 1 || s.channels[0].zones.size() < 2)
        return;
    // the zones cover every key without gaps; FT2's C-4 is LYNX's key 60 (libxmp's note numbers): the map's first 48 keys
    // (C-0..B-3) play the first sample, the rest the second
    const auto& z = s.channels[0].zones;
    for (std::size_t i = 1; i < z.size(); ++i)
        CHECK (z[i - 1].highKey + 1 == z[i].lowKey);
    CHECK (z.front().lowKey == 0 && z.back().highKey == 127);
    const auto zoneOf = [&z] (int key) -> const SampleZone* {
        for (const auto& x : z)
            if (key >= x.lowKey && key <= x.highKey)
                return &x;
        return nullptr;
    };
    CHECK (zoneOf (59) != nullptr && zoneOf (60) != nullptr && zoneOf (59)->path != zoneOf (60)->path);
    CHECK (zoneOf (59)->rootKey - zoneOf (60)->rootKey == 12); // the second sample's relative note +12
    CHECK (std::abs (s.bpm - 125.0) < 1e-6);
    // row 0 C-4 on channel 1: volume column 32 = velocity 0.5; key off on row 2 = its end
    const auto* lo = noteAt (s, 0, 0, 60);
    CHECK (lo != nullptr && std::abs (lo->velocity - 0.5f) < 1e-6f && lo->length == 480);
    // C-5 with 8FF: panned right
    const auto* hi = noteAt (s, 0, 0, 72);
    CHECK (hi != nullptr && hi->pan > 0.9f);
    // C-4 plays the second sample an octave above its rate (relative note +12): key - root = 12
    CHECK (wavRate (in.samples[0].second) == 8363 && lo != nullptr && lo->key - zoneOf (60)->rootKey == 12);
}

TEST (tracker190nw_import_s3m)
{
    using namespace tracker190nw;
    const auto in = importOf (s3mFile());
    CHECK (in.song.has_value());
    if (! in.song)
    {
        std::printf ("  (error: %s)\n", in.error.c_str());
        return;
    }
    const auto& s = *in.song;
    CHECK (s.channels.size() == 1 && s.channels[0].zones.size() == 1);
    if (s.channels.empty() || s.channels[0].zones.empty())
        return;
    const auto& z = s.channels[0].zones[0];
    // C-4 at c2spd 8363 plays the sample at its own rate; A-4 nine semitones above
    const auto* c4 = noteAt (s, 0, 0, z.rootKey);
    const auto* a4 = noteAt (s, 0, 0, z.rootKey + 9);
    CHECK (c4 != nullptr && a4 != nullptr && std::abs (z.tune) < 0.01f);
    // speed 3 from row 0: 250 BPM; the note cut on row 2 ends C-4 there
    CHECK (std::abs (s.bpm - 250.0) < 1e-6);
    CHECK (c4 != nullptr && c4->length == 480);
    CHECK (c4 != nullptr && a4 != nullptr && c4->pan < a4->pan);
}

TEST (tracker190nw_import_it)
{
    using namespace tracker190nw;
    const auto in = importOf (itFile());
    CHECK (in.song.has_value());
    if (! in.song)
    {
        std::printf ("  (error: %s)\n", in.error.c_str());
        return;
    }
    const auto& s = *in.song;
    CHECK (s.channels.size() == 1 && s.channels[0].zones.size() == 1);
    if (s.channels.empty() || s.channels[0].zones.empty())
        return;
    // c5speed 16726: an octave above 8363, so C-5 (key 60) plays the file (written at 8363 Hz) an octave up
    CHECK (s.channels[0].zones[0].rootKey == 48);
    CHECK (std::abs (s.bpm - 150.0) < 1e-6); // tempo 150, speed 6
    const auto* cut = noteAt (s, 0, 0, 60);
    const auto* delayed = noteAt (s, 0, 120, 60);
    CHECK (cut != nullptr && cut->length == 480 && cut->pan < 0.0f);
    CHECK (delayed != nullptr && delayed->length == 360 && delayed->pan > 0.0f);
}

TEST (tracker190nw_import_tempo_markers_and_damaged_files)
{
    using namespace tracker190nw;
    // a speed change in the middle of the song: a tempo marker there
    auto bytes = modFile();
    // pattern 1, row 4, channel 4: F06 (speed 6 = 125 BPM from there)
    const auto at = 20 + 31 * 30 + 2 + 128 + 4 + (((1 * 64 + 4) * 4 + 3) * 4);
    bytes[(std::size_t) at + 2] = 0x0F;
    bytes[(std::size_t) at + 3] = 0x06;
    const auto in = importOf (bytes);
    CHECK (in.song.has_value());
    if (in.song)
    {
        const auto marks = tempomarkers::list (*in.song);
        CHECK (marks.size() == 1 && marks[0].tick == (17 + 4) * 240 && std::abs (marks[0].bpm - 125.0) < 0.01);
        CHECK (in.summary.tempoChanges == 1);
    }
    // damaged and cut-off files: an error or a song, never a crash
    for (const auto& file : { modFile(), xmFile(), s3mFile(), itFile() })
        for (const auto cut : { std::size_t (0), std::size_t (63), std::size_t (200), file.size() / 2, file.size() - 1 })
        {
            const std::vector<std::uint8_t> part (file.begin(), file.begin() + (std::ptrdiff_t) std::min (cut, file.size()));
            (void) importOf (part);
        }
    std::mt19937 rng (7);
    for (int i = 0; i < 50; ++i)
    {
        auto f = itFile();
        for (int k = 0; k < 8; ++k)
            f[rng() % f.size()] = (std::uint8_t) rng();
        (void) importOf (f);
    }
    CHECK (! importOf ({ 1, 2, 3 }).song.has_value());
    CHECK (isModuleFileName ("Song.XM") && isModuleFileName ("a.it") && ! isModuleFileName ("a.flp"));
}
