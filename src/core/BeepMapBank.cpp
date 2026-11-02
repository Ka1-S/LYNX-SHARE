// LYNX's own BeepMap bank (round 168q). Designed for LYNX — never converted from FL's preset files. BeepMap plays a
// bitmap as a bank of sine oscillators (core/BeepMap.h, REA_rodada65 §2), so every preset here is a small picture drawn
// by code: one row per oscillator, the columns played left to right, red = left level, green = right level.
//
// Two ways of drawing (REA_rodada168q §2):
// - "Harmonics" scale: row r of an H-row image sounds harmonic (H − r) of the note two octaves down, so the played
//   note is harmonic 4 and an organ's drawbars are harmonics 2 (16'), 4 (8'), 6 (5⅓'), 8 (4'), 12 (2⅔'), 16 (2'),
//   20 (1⅗'), 24 (1⅓') and 32 (1'). "Frequency" sets the high roll-off.
// - "Log" scale: rows spaced in cents over a span of "Frequency" cents; with H rows and a span of (H − 1)·100 the rows
//   are semitones and the middle row is the played note — chords and arpeggios drawn as notes.
// "Length" is a column's duration: with "Loop" and one column it is the attack; without "Loop" the image is the
// envelope and the note ends after its last column.
//
// Levels: a lit pixel of value v sounds at v / (255·√H); every column is scaled so that its levels add up to at most
// `peak` (the sum is the worst case of the oscillators in phase), so no preset can clip on its own.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

#include "core/BeepMap.h"
#include "core/LynxBanks.h"

namespace lynx::banks
{
namespace
{
// a drawing: levels 0…1 for the left and right channels, and the blue (row pitch shrink, used only with "Use blue")
struct Picture
{
    int w = 1, h = 1;
    std::vector<float> left, right;

    Picture (int width, int height) : w (width), h (height), left ((std::size_t) (width * height), 0.0f), right (left) {}

    void set (int x, int y, float l, float r)
    {
        if (x < 0 || x >= w || y < 0 || y >= h)
            return;
        left[(std::size_t) (y * w + x)] = std::max (left[(std::size_t) (y * w + x)], std::clamp (l, 0.0f, 1.0f));
        right[(std::size_t) (y * w + x)] = std::max (right[(std::size_t) (y * w + x)], std::clamp (r, 0.0f, 1.0f));
    }
    void set (int x, int y, float level) { set (x, y, level, level); }
};

struct Beep
{
    int frequency = 7200, length = 2048, scale = 2;
    bool blue = false, grainy = false, loop = true, widen = false;
    float peak = 0.55f;

    // the saved state: version 0, the 15 int32 settings, the bitmap as a 24-bit bottom-up BMP stream
    std::vector<std::uint8_t> state (const Picture& p) const
    {
        const auto rootH = std::sqrt ((double) p.h);
        std::vector<std::uint8_t> pix ((std::size_t) (p.w * p.h * 2), 0); // (left, right) bytes
        for (int x = 0; x < p.w; ++x)
        {
            double sumL = 0.0, sumR = 0.0;
            for (int y = 0; y < p.h; ++y)
                sumL += p.left[(std::size_t) (y * p.w + x)], sumR += p.right[(std::size_t) (y * p.w + x)];
            // level of value 1.0 so that the column's sum stays under `peak`: v·255/(255·√H) summed ≤ peak
            const auto most = std::max (sumL, sumR);
            const auto gain = most <= 0.0 ? 1.0 : std::min (1.0, (double) peak * rootH / most);
            for (int y = 0; y < p.h; ++y)
            {
                const auto at = (std::size_t) (y * p.w + x);
                pix[2 * at] = (std::uint8_t) std::lround (std::clamp (p.left[at] * gain, 0.0, 1.0) * 255.0);
                pix[2 * at + 1] = (std::uint8_t) std::lround (std::clamp (p.right[at] * gain, 0.0, 1.0) * 255.0);
            }
        }
        const auto stride = (std::size_t) ((p.w * 3 + 3) / 4 * 4);
        const auto imageBytes = stride * (std::size_t) p.h;
        std::vector<std::uint8_t> bmp (54 + imageBytes, 0);
        const auto put32 = [&bmp] (std::size_t at, std::uint32_t v) { std::memcpy (bmp.data() + at, &v, 4); };
        bmp[0] = 'B', bmp[1] = 'M';
        put32 (2, (std::uint32_t) bmp.size());
        put32 (10, 54);
        put32 (14, 40);
        put32 (18, (std::uint32_t) p.w);
        put32 (22, (std::uint32_t) p.h);
        bmp[26] = 1;
        bmp[28] = 24;
        put32 (34, (std::uint32_t) imageBytes);
        for (int y = 0; y < p.h; ++y)
        {
            auto* row = bmp.data() + 54 + stride * (std::size_t) (p.h - 1 - y); // bottom-up
            for (int x = 0; x < p.w; ++x)
            {
                const auto at = (std::size_t) (y * p.w + x);
                row[3 * x + 0] = 0;              // blue
                row[3 * x + 1] = pix[2 * at + 1]; // green = right
                row[3 * x + 2] = pix[2 * at];     // red = left
            }
        }
        std::vector<std::uint8_t> s;
        const auto add32 = [&s] (std::int32_t v) {
            std::uint8_t b[4];
            std::memcpy (b, &v, 4);
            s.insert (s.end(), b, b + 4);
        };
        add32 (0);
        const std::int32_t settings[15] = { frequency, length, scale, blue ? 1 : 0, grainy ? 1 : 0, loop ? 1 : 0, widen ? 1 : 0,
                                            0, 0, 0, 0, 0, 0, 0, std::max (3, p.h) };
        for (const auto v : settings)
            add32 (v);
        add32 ((std::int32_t) bmp.size());
        s.insert (s.end(), bmp.begin(), bmp.end());
        return s;
    }
};

Preset make (const char* name, const char* category, std::vector<std::string> tags, std::function<std::vector<std::uint8_t>()> build)
{
    auto state = build();
    Preset p;
    p.name = name, p.category = category, p.tags = std::move (tags);
    p.apply = [state] (ChannelDef& c) { c.pluginState = state; };
    return p;
}

// ---------------------------------------------------------------------------------------------- harmonics drawings

constexpr int organRows = 32;
// the drawbars' harmonics of the note two octaves down: 16', 5⅓', 8', 4', 2⅔', 2', 1⅗', 1⅓', 1'
constexpr int drawbarHarmonic[9] = { 2, 6, 4, 8, 12, 16, 20, 24, 32 };

// a drawbar 0…8 as a level: about 3 dB a step, 0 = off
float drawbarLevel (int d) { return d <= 0 ? 0.0f : std::pow (10.0f, -3.0f * (float) (8 - d) / 20.0f); }

// row of harmonic n in an H-row harmonics image
int harmonicRow (int n, int h) { return h - n; }

// an organ registration ("888000000") held: one looped column
std::vector<std::uint8_t> organ (const char* drawbars, int attackLength, bool widen)
{
    Picture p (1, organRows);
    for (int i = 0; i < 9 && drawbars[i] != 0; ++i)
        p.set (0, harmonicRow (drawbarHarmonic[i], organRows), drawbarLevel (drawbars[i] - '0'));
    Beep b;
    b.scale = 2, b.frequency = 9600, b.length = attackLength, b.widen = widen;
    return b.state (p);
}

// harmonic k (1 = the played note) of the played note, in a harmonics image: harmonic 4k of the note two octaves down
int noteHarmonicRow (int k, int h) { return h - 4 * k; }

std::vector<std::uint8_t> waveform (bool oddOnly, int rolloff)
{
    constexpr int h = 64;
    Picture p (1, h);
    for (int k = 1; 4 * k <= h; ++k)
        if (! oddOnly || k % 2 == 1)
            p.set (0, noteHarmonicRow (k, h), 1.0f / (float) k);
    Beep b;
    b.scale = 2, b.frequency = rolloff, b.length = 600;
    return b.state (p);
}

// partials (harmonic of the note two octaves down, level, decay per column) fading over `columns` columns, no loop
struct Partial { int harmonic; float level, decay; };

std::vector<std::uint8_t> struck (int h, const std::vector<Partial>& partials, int columns, int length, bool widen, int rolloff = 9600)
{
    Picture p (columns, h);
    for (int x = 0; x < columns; ++x)
        for (const auto& q : partials)
        {
            const auto lv = q.level * std::pow (q.decay, (float) x);
            // a little stereo motion: odd harmonics lean left, even right, as the note fades
            const auto lean = 0.12f * (float) x / (float) columns * (q.harmonic % 2 == 0 ? 1.0f : -1.0f);
            p.set (x, harmonicRow (q.harmonic, h), lv * (1.0f - lean), lv * (1.0f + lean));
        }
    Beep b;
    b.scale = 2, b.frequency = rolloff, b.length = length, b.loop = false, b.widen = widen;
    return b.state (p);
}

// ---------------------------------------------------------------------------------------------- note drawings (Log)

constexpr int noteRows = 25; // ±12 semitones around the played note

int semitoneRow (int semitones) { return (noteRows - 1) / 2 - semitones; }

Beep notes (int length, bool loop)
{
    Beep b;
    b.scale = 0, b.frequency = (noteRows - 1) * 100, b.length = length, b.loop = loop;
    return b;
}

// one chord per column (each a list of semitones), every note doubled an octave down when it fits, quieter
std::vector<std::uint8_t> sequence (const std::vector<std::vector<int>>& steps, int length, bool loop, bool widen, float fade = 1.0f, bool doubled = true)
{
    Picture p ((int) steps.size(), noteRows);
    for (int x = 0; x < (int) steps.size(); ++x)
    {
        const auto lv = std::pow (fade, (float) x);
        for (const auto n : steps[(std::size_t) x])
        {
            p.set (x, semitoneRow (n), lv);
            if (doubled && n - 12 >= -12)
                p.set (x, semitoneRow (n - 12), 0.45f * lv);
        }
    }
    auto b = notes (length, loop);
    b.widen = widen;
    return b.state (p);
}
} // namespace

std::vector<Preset> beepMapBank()
{
    std::vector<Preset> bank;
    // --- organs (harmonics, one looped column: the registration)
    bank.push_back (make ("Jazz Drawbars", "Organ", { "drawbar", "jazz" }, [] { return organ ("888000000", 300, false); }));
    bank.push_back (make ("Gospel Full", "Organ", { "drawbar", "full" }, [] { return organ ("888800000", 300, false); }));
    bank.push_back (make ("Stops Out", "Organ", { "drawbar", "bright" }, [] { return organ ("888888888", 300, false); }));
    bank.push_back (make ("Soft Flute Stop", "Organ", { "drawbar", "soft" }, [] { return organ ("008400000", 1279, false); }));
    bank.push_back (make ("Reed Organ", "Organ", { "drawbar", "reed" }, [] { return organ ("806000088", 600, true); }));
    // the percussion click: 2⅔' fading over the first columns, the registration held for a long image
    bank.push_back (make ("Click Organ", "Organ", { "drawbar", "percussion" }, [] {
        Picture p (48, organRows);
        for (int x = 0; x < 48; ++x)
        {
            for (int i = 0; i < 3; ++i)
                p.set (x, harmonicRow (drawbarHarmonic[i], organRows), drawbarLevel (8));
            p.set (x, harmonicRow (12, organRows), 0.9f * std::pow (0.45f, (float) x));
        }
        Beep b;
        b.scale = 2, b.frequency = 9600, b.length = 4096, b.loop = false;
        return b.state (p);
    }));
    // --- leads and basses (harmonic series of the played note)
    bank.push_back (make ("Additive Saw", "Lead", { "saw", "additive" }, [] { return waveform (false, 7200); }));
    bank.push_back (make ("Additive Square", "Lead", { "square", "hollow" }, [] { return waveform (true, 6000); }));
    bank.push_back (make ("Sub Bass", "Bass", { "sub", "sine" }, [] {
        Picture p (1, organRows);
        p.set (0, harmonicRow (2, organRows), 1.0f);
        p.set (0, harmonicRow (4, organRows), 0.35f);
        p.set (0, harmonicRow (6, organRows), 0.12f);
        Beep b;
        b.scale = 2, b.frequency = 4800, b.length = 200;
        return b.state (p);
    }));
    bank.push_back (make ("Pluck Bass", "Bass", { "pluck", "decay" }, [] {
        std::vector<Partial> q;
        for (int k = 1; k <= 8; ++k)
            q.push_back ({ 2 * k, 1.0f / (float) k, std::pow (0.86f, (float) k) });
        return struck (organRows, q, 24, 1279, false);
    }));
    // --- keys and bells (struck partials fading, no loop)
    bank.push_back (make ("Music Box", "Keys", { "bell", "tine" }, [] {
        return struck (64, { { 4, 1.0f, 0.93f }, { 16, 0.45f, 0.85f }, { 40, 0.25f, 0.75f }, { 8, 0.2f, 0.9f } }, 40, 1279, false);
    }));
    bank.push_back (make ("Tubular Bell", "Keys", { "bell", "long" }, [] {
        return struck (64, { { 2, 0.5f, 0.95f }, { 4, 1.0f, 0.93f }, { 5, 0.45f, 0.9f }, { 6, 0.5f, 0.91f }, { 10, 0.35f, 0.86f },
                             { 11, 0.3f, 0.85f }, { 17, 0.25f, 0.8f }, { 27, 0.2f, 0.75f } },
                       40, 3340, true);
    }));
    bank.push_back (make ("Glass Keys", "Keys", { "glass", "electric" }, [] {
        return struck (64, { { 4, 1.0f, 0.92f }, { 8, 0.3f, 0.9f }, { 28, 0.35f, 0.7f }, { 56, 0.2f, 0.6f } }, 32, 2048, true, 8400);
    }));
    // --- pads (looped images that morph)
    bank.push_back (make ("Breathing Pad", "Pad", { "morph", "slow" }, [] {
        constexpr int h = 64, w = 16;
        Picture p (w, h);
        for (int x = 0; x < w; ++x)
        {
            const auto t = 0.5f - 0.5f * std::cos (6.2831853f * (float) x / (float) w); // 0 → 1 → 0
            for (int k = 1; k <= 8; ++k)
            {
                const auto base = 1.0f / (float) k;
                const auto odd = k % 2 == 1 ? 1.0f : t; // the even harmonics come and go
                p.set (x, noteHarmonicRow (k, h), base * odd * (1.0f - 0.3f * t), base * odd);
            }
            p.set (x, harmonicRow (2, h), 0.5f);
        }
        Beep b;
        b.scale = 2, b.frequency = 7200, b.length = 4096, b.widen = true;
        return b.state (p);
    }));
    bank.push_back (make ("Beating Choir", "Pad", { "chorus", "detuned" }, [] {
        // seven rows a few cents apart around the note ("Log", a span of 36 cents) beat against each other
        Picture p (1, 7);
        for (int y = 0; y < 7; ++y)
            p.set (0, y, y == 3 ? 1.0f : 0.75f - 0.05f * (float) y, y == 3 ? 1.0f : 0.45f + 0.05f * (float) y);
        Beep b;
        b.scale = 0, b.frequency = 36, b.length = 4096, b.widen = true;
        return b.state (p);
    }));
    // --- chords and arpeggios (Log scale, semitone rows)
    bank.push_back (make ("Major Arp", "Arp", { "arpeggio", "major" }, [] {
        return sequence ({ { 0 }, { 4 }, { 7 }, { 12 }, { 7 }, { 4 } }, 4096, true, false);
    }));
    bank.push_back (make ("Minor Arp", "Arp", { "arpeggio", "minor" }, [] {
        return sequence ({ { 0 }, { 3 }, { 7 }, { 10 }, { 12 }, { 10 }, { 7 }, { 3 } }, 3600, true, true);
    }));
    bank.push_back (make ("Minor Stab", "Chord", { "chord", "stab", "minor" }, [] {
        std::vector<std::vector<int>> steps (10, { 0, 3, 7, 12 });
        return sequence (steps, 2048, false, true, 0.7f);
    }));
    bank.push_back (make ("Seventh Pad", "Chord", { "chord", "pad", "major 7" }, [] {
        return sequence ({ { -12, 0, 4, 7, 11 } }, 4096, true, true, 1.0f, false);
    }));
    // --- effects
    bank.push_back (make ("Rising Sweep", "FX", { "sweep", "riser" }, [] {
        constexpr int w = 48;
        Picture p (w, noteRows);
        for (int x = 0; x < w; ++x)
        {
            const auto y = noteRows - 1 - (x * (noteRows - 1)) / (w - 1);
            p.set (x, y, 1.0f);
            p.set (x, std::min (noteRows - 1, y + 1), 0.4f);
        }
        auto b = notes (2048, false);
        b.frequency = 4800; // (whole-tone rows: a stepped glide over four octaves)
        b.widen = true;
        return b.state (p);
    }));
    bank.push_back (make ("Grain Sparkle", "FX", { "grainy", "random" }, [] {
        constexpr int w = 32, h = 49;
        Picture p (w, h);
        std::uint32_t seed = 0x1234567u;
        const auto next = [&seed] { return seed = seed * 1664525u + 1013904223u; };
        for (int x = 0; x < w; ++x)
            for (int i = 0; i < 3; ++i)
            {
                const auto y = (int) ((next() >> 8) % (std::uint32_t) h);
                const auto pan = (float) ((next() >> 8) % 1000u) / 1000.0f;
                p.set (x, y, 1.0f - 0.6f * pan, 0.4f + 0.6f * pan);
            }
        Beep b;
        b.scale = 0, b.frequency = 4800, b.length = 2600, b.grainy = true, b.widen = true;
        return b.state (p);
    }));
    return bank;
}
} // namespace lynx::banks
