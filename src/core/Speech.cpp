// LYNX's speech synthesizer (round 168b): see core/Speech.h. The .speech format, the defaults, the text syntax and the
// pitch rules follow FL's own code (docs/analise/REA_rodada168b.md §A); the voice is LYNX's own formant synthesizer.

#include "core/Speech.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#ifndef TR_NOOP
 #define TR_NOOP(text) text // marks a literal for translation (tools/i18n_keys.py scans core too)
#endif

namespace lynx::speech
{
namespace
{
constexpr double pi = 3.14159265358979323846;

const char* const personalityNames[personalityCount] = {
    TR_NOOP ("Male"), TR_NOOP ("Female"), TR_NOOP ("Large male"), TR_NOOP ("Child"), TR_NOOP ("Giant male"), TR_NOOP ("Mellow female"), TR_NOOP ("Mellow male"), TR_NOOP ("Crisp male"), TR_NOOP ("Fly"),
    TR_NOOP ("Robotoid"), TR_NOOP ("Martian"), TR_NOOP ("Colossus"), TR_NOOP ("Fast fred"), TR_NOOP ("Old woman"), TR_NOOP ("Munchkin"), TR_NOOP ("Troll"), TR_NOOP ("Nerd"), TR_NOOP ("Milktoast"), TR_NOOP ("Tipsy"),
    TR_NOOP ("Choirboy") };
const char* const personalityHints[personalityCount] = {
    "Default male voice", "Default female voice", "Default large male voice", "Default child voice",
    "Low pitched male", "Sexy female", "Mellow male", "Crisp, clear male", "High pitched, quavering voice", "Robotoid",
    "Alien", "The voice of the Colossus computer", "Fast talking voice", "Elderly woman", "Dwarf",
    "A little larger than a dwarf", "Nerdish male", "Whiney male", "Drunken male", "High pitched, sung voice" };

// ---- LYNX's voices: how each personality colours the synthesizer ----
struct Voice
{
    double formantScale = 1.0;   // vocal tract length (higher = smaller speaker)
    double rateScale = 1.0;      // speaking speed on top of the word rate
    double breath = 0.0;         // aspiration noise mixed into the voicing
    double tilt = 0.5;           // 0 bright .. 1 dark glottal pulse
    double jitter = 0.004;       // random pitch flutter (fraction)
    double vibratoDepth = 0.0;   // semitones
    double vibratoRate = 5.5;    // Hz
    double ring = 0.0;           // ring modulation mix (Martian)
    double ringHz = 0.0;
    double comb = 0.0;           // metallic comb feedback (Robotoid, Colossus)
    double combMs = 0.0;
    double buzz = 0.0;           // narrow pulse instead of a smooth glottal pulse (robots)
    double nasal = 0.0;          // extra nasal colour (Nerd, Milktoast)
    double slur = 0.0;           // longer transitions and pitch wobble (Tipsy)
    double whine = 0.0;          // pitch rising through each word (Milktoast)
};

Voice voiceFor (int personality)
{
    Voice v;
    switch (personality)
    {
        case 0: break;
        case 1: v.formantScale = 1.17; v.breath = 0.05; v.tilt = 0.45; break;
        case 2: v.formantScale = 0.92; v.tilt = 0.55; break;
        case 3: v.formantScale = 1.3; v.tilt = 0.4; break;
        case 4: v.formantScale = 0.82; v.rateScale = 0.9; v.tilt = 0.65; break;
        case 5: v.formantScale = 1.14; v.breath = 0.14; v.tilt = 0.75; break;
        case 6: v.formantScale = 0.98; v.breath = 0.1; v.tilt = 0.75; break;
        case 7: v.formantScale = 1.02; v.tilt = 0.25; break;
        case 8: v.formantScale = 1.35; v.vibratoDepth = 0.7; v.vibratoRate = 9.0; v.tilt = 0.35; break;
        case 9: v.buzz = 1.0; v.jitter = 0.0; v.comb = 0.3; v.combMs = 2.3; v.tilt = 0.2; break;
        case 10: v.formantScale = 1.1; v.ring = 0.55; v.ringHz = 170.0; v.jitter = 0.0; break;
        case 11: v.formantScale = 0.95; v.buzz = 0.7; v.comb = 0.6; v.combMs = 6.0; v.jitter = 0.0; v.tilt = 0.3; break;
        case 12: v.rateScale = 1.45; break;
        case 13: v.formantScale = 1.12; v.vibratoDepth = 0.45; v.vibratoRate = 6.5; v.jitter = 0.02; v.breath = 0.1; break;
        case 14: v.formantScale = 1.45; v.rateScale = 1.1; break;
        case 15: v.formantScale = 1.25; break;
        case 16: v.formantScale = 1.05; v.nasal = 0.6; v.tilt = 0.35; break;
        case 17: v.formantScale = 1.08; v.nasal = 0.45; v.whine = 0.25; break;
        case 18: v.formantScale = 0.98; v.rateScale = 0.85; v.slur = 1.0; v.jitter = 0.012; break;
        case 19: v.formantScale = 1.3; v.vibratoDepth = 0.3; v.vibratoRate = 5.0; v.breath = 0.05; break;
        default: break;
    }
    return v;
}

// ---- phonemes ----
enum class Kind { vowel, glide, liquid, nasal, fricative, stop, affricate, aspirate, pause };

struct Phoneme
{
    const char* code;
    Kind kind;
    bool voiced;
    float f1, f2, f3;          // targets (Hz) for an adult male; diphthongs end at f1b..f3b
    float f1b, f2b, f3b;
    float ms;                  // duration at 150 WPM
    float noiseHz, noiseBw, noiseGain; // frication (fricatives, bursts)
    float closureMs;           // stops: the closure before the burst
};

// Formant targets: commonly published averages for American English (Peterson & Barney 1952, Klatt 1980 style values).
const Phoneme phonemes[] = {
    // vowels
    { "IY", Kind::vowel, true, 300, 2250, 2950, 0, 0, 0, 120, 0, 0, 0, 0 },
    { "IH", Kind::vowel, true, 400, 1900, 2550, 0, 0, 0, 85, 0, 0, 0, 0 },
    { "EY", Kind::vowel, true, 480, 1850, 2550, 320, 2200, 2900, 150, 0, 0, 0, 0 },
    { "EH", Kind::vowel, true, 550, 1750, 2500, 0, 0, 0, 95, 0, 0, 0, 0 },
    { "AE", Kind::vowel, true, 680, 1680, 2420, 0, 0, 0, 150, 0, 0, 0, 0 },
    { "AA", Kind::vowel, true, 720, 1150, 2550, 0, 0, 0, 150, 0, 0, 0, 0 },
    { "AO", Kind::vowel, true, 580, 900, 2500, 0, 0, 0, 150, 0, 0, 0, 0 },
    { "OW", Kind::vowel, true, 520, 1000, 2400, 380, 850, 2300, 150, 0, 0, 0, 0 },
    { "UH", Kind::vowel, true, 450, 1100, 2350, 0, 0, 0, 85, 0, 0, 0, 0 },
    { "UW", Kind::vowel, true, 320, 950, 2250, 0, 0, 0, 130, 0, 0, 0, 0 },
    { "AH", Kind::vowel, true, 620, 1220, 2550, 0, 0, 0, 90, 0, 0, 0, 0 },
    { "AX", Kind::vowel, true, 500, 1450, 2450, 0, 0, 0, 55, 0, 0, 0, 0 },
    { "ER", Kind::vowel, true, 480, 1350, 1650, 0, 0, 0, 140, 0, 0, 0, 0 },
    { "AY", Kind::vowel, true, 700, 1250, 2550, 380, 2000, 2650, 190, 0, 0, 0, 0 },
    { "AW", Kind::vowel, true, 700, 1250, 2550, 420, 900, 2350, 190, 0, 0, 0, 0 },
    { "OY", Kind::vowel, true, 560, 900, 2450, 380, 1900, 2600, 210, 0, 0, 0, 0 },
    // glides, liquids
    { "W", Kind::glide, true, 300, 650, 2200, 0, 0, 0, 65, 0, 0, 0, 0 },
    { "Y", Kind::glide, true, 280, 2150, 3000, 0, 0, 0, 60, 0, 0, 0, 0 },
    { "R", Kind::liquid, true, 330, 1100, 1450, 0, 0, 0, 65, 0, 0, 0, 0 },
    { "L", Kind::liquid, true, 330, 1050, 2800, 0, 0, 0, 65, 0, 0, 0, 0 },
    // nasals
    { "M", Kind::nasal, true, 300, 1100, 2150, 0, 0, 0, 70, 0, 0, 0, 0 },
    { "N", Kind::nasal, true, 300, 1500, 2550, 0, 0, 0, 65, 0, 0, 0, 0 },
    { "NG", Kind::nasal, true, 300, 2000, 2750, 0, 0, 0, 75, 0, 0, 0, 0 },
    // fricatives (formants: where they pull the neighbouring vowels)
    { "F", Kind::fricative, false, 340, 1100, 2300, 0, 0, 0, 100, 5500, 4000, 0.12f, 0 },
    { "V", Kind::fricative, true, 300, 1100, 2300, 0, 0, 0, 70, 5000, 4000, 0.07f, 0 },
    { "TH", Kind::fricative, false, 320, 1400, 2700, 0, 0, 0, 95, 6500, 4000, 0.1f, 0 },
    { "DH", Kind::fricative, true, 300, 1500, 2700, 0, 0, 0, 55, 5000, 4000, 0.06f, 0 },
    { "S", Kind::fricative, false, 320, 1500, 2600, 0, 0, 0, 105, 6000, 1800, 0.45f, 0 },
    { "Z", Kind::fricative, true, 300, 1500, 2600, 0, 0, 0, 80, 5800, 1800, 0.25f, 0 },
    { "SH", Kind::fricative, false, 320, 1800, 2400, 0, 0, 0, 110, 2900, 1400, 0.5f, 0 },
    { "ZH", Kind::fricative, true, 300, 1800, 2400, 0, 0, 0, 80, 2800, 1400, 0.28f, 0 },
    { "HH", Kind::aspirate, false, 0, 0, 0, 0, 0, 0, 60, 0, 0, 0, 0 },
    // stops: closure, then a burst
    { "P", Kind::stop, false, 300, 900, 2200, 0, 0, 0, 25, 1200, 1800, 0.35f, 65 },
    { "B", Kind::stop, true, 300, 900, 2200, 0, 0, 0, 15, 1200, 1800, 0.2f, 55 },
    { "T", Kind::stop, false, 320, 1750, 2600, 0, 0, 0, 25, 4500, 2500, 0.5f, 60 },
    { "D", Kind::stop, true, 320, 1700, 2600, 0, 0, 0, 15, 4000, 2500, 0.3f, 50 },
    { "K", Kind::stop, false, 320, 1900, 2300, 0, 0, 0, 30, 2300, 1200, 0.5f, 65 },
    { "G", Kind::stop, true, 320, 1900, 2300, 0, 0, 0, 20, 2200, 1200, 0.3f, 55 },
    { "CH", Kind::affricate, false, 320, 1800, 2400, 0, 0, 0, 90, 2900, 1400, 0.5f, 55 },
    { "JH", Kind::affricate, true, 300, 1800, 2400, 0, 0, 0, 70, 2800, 1400, 0.3f, 45 },
    { "_", Kind::pause, false, 0, 0, 0, 0, 0, 0, 180, 0, 0, 0, 0 },
};

const Phoneme* findPhoneme (const std::string& code)
{
    for (const auto& p : phonemes)
        if (code == p.code)
            return &p;
    return nullptr;
}

// ---- small deterministic noise / random source ----
struct Rng
{
    std::uint32_t state;
    explicit Rng (std::uint32_t seed) : state (seed != 0 ? seed : 0x9e3779b9u) {}
    std::uint32_t next() noexcept { state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state; }
    double uniform() noexcept { return (next() >> 8) * (1.0 / 16777216.0); }       // [0, 1)
    double bipolar() noexcept { return uniform() * 2.0 - 1.0; }
};

std::uint32_t hashText (std::string_view s) noexcept
{
    std::uint32_t h = 2166136261u;
    for (const auto c : s)
        h = (h ^ (unsigned char) c) * 16777619u;
    return h;
}

// ---- resonators (Klatt-style two-pole sections) ----
struct Resonator
{
    double a = 0, b = 0, c = 0, y1 = 0, y2 = 0;
    void set (double freq, double bw, double sr) noexcept
    {
        freq = std::clamp (freq, 20.0, sr * 0.45);
        const double r = std::exp (-pi * bw / sr);
        c = -r * r;
        b = 2.0 * r * std::cos (2.0 * pi * freq / sr);
        a = 1.0 - b - c;
    }
    double process (double x) noexcept
    {
        const double y = a * x + b * y1 + c * y2;
        y2 = y1;
        y1 = y;
        return y;
    }
};

struct AntiResonator
{
    double a = 1, b = 0, c = 0, x1 = 0, x2 = 0;
    void set (double freq, double bw, double sr) noexcept
    {
        freq = std::clamp (freq, 20.0, sr * 0.45);
        const double r = std::exp (-pi * bw / sr);
        const double cc = -r * r, bb = 2.0 * r * std::cos (2.0 * pi * freq / sr), aa = 1.0 - bb - cc;
        a = 1.0 / aa;
        b = -bb / aa;
        c = -cc / aa;
    }
    double process (double x) noexcept
    {
        const double y = a * x + b * x1 + c * x2;
        x2 = x1;
        x1 = x;
        return y;
    }
};

// ---- one chunk of speech as a list of timed segments ----
struct Segment
{
    const Phoneme* ph = nullptr;
    bool stressed = false;
    double seconds = 0.0;
    double noteOffset = 0.0;  // semitones (Random style: per syllable)
};

struct Track
{
    // per-sample targets, filled frame by frame
    double f1, f2, f3, av, ah, af, fn, fbw, nasal, f0semis;
};

int clampInt (int v, int lo, int hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }

// The phonemes of one chunk to segments with durations.
std::vector<Segment> segmentsFor (const std::string& phonemeString, const Settings& s, const Voice& v, Rng& rng)
{
    std::vector<Segment> out;
    std::size_t start = 0;
    while (start < phonemeString.size())
    {
        auto end = phonemeString.find (' ', start);
        if (end == std::string::npos) end = phonemeString.size();
        if (end > start)
        {
            auto code = phonemeString.substr (start, end - start);
            bool stressed = false;
            if (! code.empty() && code.back() == '1')
            {
                stressed = true;
                code.pop_back();
            }
            if (const auto* p = findPhoneme (code))
            {
                Segment seg;
                seg.ph = p;
                seg.stressed = stressed;
                out.push_back (seg);
            }
        }
        start = end + 1;
    }

    const double rate = std::clamp ((double) s.rate, (double) minRate, (double) maxRate) * v.rateScale;
    const double scale = 150.0 / std::max (10.0, rate);
    static const int majorScale[] = { 0, 2, 4, 5, 7, 9, 11, 12 };
    double lastNote = 0.0;
    for (std::size_t i = 0; i < out.size(); ++i)
    {
        auto& seg = out[i];
        double ms = seg.ph->ms;
        if (seg.ph->kind == Kind::vowel)
        {
            if (seg.stressed) ms *= 1.25;
            // the last vowel of the chunk is longer (phrase-final lengthening)
            bool lastVowel = true;
            for (auto j = i + 1; j < out.size(); ++j)
                if (out[j].ph->kind == Kind::vowel) { lastVowel = false; break; }
            if (lastVowel) ms *= 1.3;
            if (s.style == Style::sing) ms *= 1.7;
            if (s.style == Style::random)
                lastNote = majorScale[rng.next() % 8];
        }
        if (seg.ph->kind == Kind::stop || seg.ph->kind == Kind::affricate)
            ms += seg.ph->closureMs;
        seg.noteOffset = lastNote;
        // pauses keep about the same length at any rate (a comma is a comma)
        seg.seconds = seg.ph->kind == Kind::pause ? ms * 0.001 * std::sqrt (scale) : ms * 0.001 * scale;
    }
    return out;
}

// The formant targets of a segment, at its start (t = 0) and end (t = 1).
void targets (const Segment& seg, const Segment* next, const Segment* prev, double t, double& f1, double& f2, double& f3)
{
    const auto* p = seg.ph;
    if (p->kind == Kind::aspirate || p->kind == Kind::pause)
    {
        // HH takes the shape of the vowel that follows (or the one before)
        const auto* other = next != nullptr && next->ph->kind != Kind::pause ? next->ph : (prev != nullptr ? prev->ph : nullptr);
        if (other != nullptr && other->f1 > 0)
        {
            f1 = other->f1; f2 = other->f2; f3 = other->f3;
            return;
        }
        f1 = 500; f2 = 1500; f3 = 2500;
        return;
    }
    if (p->f1b > 0)
    {
        const double k = std::clamp ((t - 0.25) / 0.6, 0.0, 1.0);
        f1 = p->f1 + (p->f1b - p->f1) * k;
        f2 = p->f2 + (p->f2b - p->f2) * k;
        f3 = p->f3 + (p->f3b - p->f3) * k;
        return;
    }
    f1 = p->f1; f2 = p->f2; f3 = p->f3;
}

double voicingOf (const Phoneme& p) noexcept
{
    switch (p.kind)
    {
        case Kind::vowel: return 1.0;
        case Kind::glide: return 0.8;
        case Kind::liquid: return 0.75;
        case Kind::nasal: return 0.6;
        case Kind::fricative: return p.voiced ? 0.45 : 0.0;
        case Kind::stop: return p.voiced ? 0.2 : 0.0;
        case Kind::affricate: return p.voiced ? 0.35 : 0.0;
        case Kind::aspirate: case Kind::pause: return 0.0;
    }
    return 0.0;
}

// Synthesises one chunk at pitch f0 (Hz). Appends to `out`.
void synthesizeChunk (const std::string& chunkText, double f0, const Settings& s, const Voice& v, double sr, Rng& rng,
                      std::vector<float>& out)
{
    auto segs = segmentsFor (toPhonemes (chunkText), s, v, rng);
    if (segs.empty())
        return;

    // segment boundaries (samples)
    std::vector<std::int64_t> starts;
    std::int64_t total = 0;
    for (const auto& seg : segs)
    {
        starts.push_back (total);
        total += std::max<std::int64_t> (1, (std::int64_t) std::llround (seg.seconds * sr));
    }
    const auto lead = (std::int64_t) (0.015 * sr), tail = (std::int64_t) (0.06 * sr);
    const auto length = lead + total + tail;

    // where the stressed vowel is (Natural style: the pitch peaks there)
    double peakPos = 0.3;
    for (std::size_t i = 0; i < segs.size(); ++i)
        if (segs[i].stressed)
        {
            peakPos = (double) (starts[i] + (std::int64_t) (segs[i].seconds * sr * 0.4)) / (double) std::max<std::int64_t> (1, total);
            break;
        }
    const bool question = chunkText.find ('?') != std::string::npos;
    const bool exclaim = chunkText.find ('!') != std::string::npos;

    Resonator r1, r2, r3, r4, r5, nasalPole, fric, glottalLow;
    AntiResonator nasalZero;
    r4.set (3300 * v.formantScale, 250, sr);
    r5.set (3850 * v.formantScale, 300, sr);
    nasalPole.set (270, 100, sr);
    std::vector<double> comb (std::max<std::size_t> (1, (std::size_t) (v.combMs * 0.001 * sr) + 1), 0.0);
    std::size_t combPos = 0;
    double phase = 0.0, prevPulse = 0.0, ringPhase = 0.0, vibPhase = 0.0, dc = 0.0, dcIn = 0.0;
    double jitterValue = 0.0;
    double lastF1 = -1, lastF2 = -1, lastF3 = -1, lastFn = -1, lastNasal = -1;
    const int frame = std::max (1, (int) (0.002 * sr)); // parameters move every 2 ms

    double f1 = 500, f2 = 1500, f3 = 2500, av = 0, ah = 0, af = 0, fn = 3000, fbw = 1500, nasal = 0, semis = 0;
    double avS = 0, ahS = 0, afS = 0; // smoothed amplitudes
    std::size_t seg = 0;
    const auto base = out.size();
    out.resize (base + (std::size_t) length, 0.0f);

    for (std::int64_t n = 0; n < length; ++n)
    {
        const auto t = n - lead;
        if (n % frame == 0)
        {
            av = ah = af = 0.0;
            if (t >= 0 && t < total)
            {
                while (seg + 1 < segs.size() && t >= starts[seg + 1]) ++seg;
                const auto& cur = segs[seg];
                const auto* prev = seg > 0 ? &segs[seg - 1] : nullptr;
                const auto* next = seg + 1 < segs.size() ? &segs[seg + 1] : nullptr;
                const double segLen = (double) std::max<std::int64_t> (1, (seg + 1 < segs.size() ? starts[seg + 1] : total) - starts[seg]);
                const double u = (double) (t - starts[seg]) / segLen;

                // formants: this target, blended with the neighbours near the edges
                double tf1, tf2, tf3;
                targets (cur, next, prev, u, tf1, tf2, tf3);
                const double edge = 0.3 + 0.2 * v.slur;
                if (u < edge && prev != nullptr && prev->ph->kind != Kind::pause)
                {
                    double pf1, pf2, pf3;
                    targets (*prev, &cur, nullptr, 1.0, pf1, pf2, pf3);
                    const double k = 0.5 + 0.5 * (u / edge);
                    tf1 = pf1 + (tf1 - pf1) * k; tf2 = pf2 + (tf2 - pf2) * k; tf3 = pf3 + (tf3 - pf3) * k;
                }
                else if (u > 1.0 - edge && next != nullptr && next->ph->kind != Kind::pause)
                {
                    double nf1, nf2, nf3;
                    targets (*next, nullptr, &cur, 0.0, nf1, nf2, nf3);
                    const double k = 0.5 * ((u - (1.0 - edge)) / edge);
                    tf1 = tf1 + (nf1 - tf1) * k; tf2 = tf2 + (nf2 - tf2) * k; tf3 = tf3 + (nf3 - tf3) * k;
                }
                f1 = tf1 * v.formantScale; f2 = tf2 * v.formantScale; f3 = tf3 * v.formantScale;

                // sources
                av = voicingOf (*cur.ph);
                const auto kind = cur.ph->kind;
                if (kind == Kind::aspirate)
                    ah = 0.55;
                if (kind == Kind::fricative)
                {
                    af = cur.ph->noiseGain;
                    fn = cur.ph->noiseHz; fbw = cur.ph->noiseBw;
                }
                if (kind == Kind::stop || kind == Kind::affricate)
                {
                    const double closure = cur.ph->closureMs * 0.001 * sr / segLen;
                    if (u < closure)
                        av = cur.ph->voiced ? 0.18 : 0.0; // closure (voice bar for b d g)
                    else
                    {
                        const double after = (u - closure) / std::max (1e-6, 1.0 - closure);
                        fn = cur.ph->noiseHz; fbw = cur.ph->noiseBw;
                        if (kind == Kind::affricate)
                            af = cur.ph->noiseGain;
                        else
                        {
                            af = cur.ph->noiseGain * std::exp (-after * 4.0);  // the burst
                            if (! cur.ph->voiced)
                                ah = 0.35 * (1.0 - after);                   // aspiration before the vowel
                        }
                        if (cur.ph->voiced)
                            av = 0.3;
                    }
                }
                nasal = kind == Kind::nasal ? 1.0 : 0.0;
                if (v.nasal > 0 && av > 0.5)
                    nasal = std::max (nasal, v.nasal);

                // pitch contour (semitones from f0)
                const double x = (double) t / (double) std::max<std::int64_t> (1, total);
                switch (s.style)
                {
                    case Style::natural:
                    {
                        // rise to the stressed syllable, then fall (declination); questions end high
                        const double rise = x < peakPos ? 1.0 + 2.5 * (x / std::max (0.05, peakPos)) : 3.5 - 6.0 * ((x - peakPos) / std::max (0.05, 1.0 - peakPos));
                        semis = rise;
                        if (question && x > 0.7) semis += 8.0 * (x - 0.7) / 0.3;
                        if (exclaim) semis *= 1.4;
                        break;
                    }
                    case Style::monotone: semis = 0.0; break;
                    case Style::sing: semis = 0.0; break;
                    case Style::random: semis = cur.noteOffset; break;
                }
                semis += v.whine * 4.0 * x;
                if (v.slur > 0)
                    semis += v.slur * 1.5 * std::sin (2.0 * pi * 1.3 * (double) n / sr);
            }
        }

        // smoothing of the source amplitudes (about 6 ms) so nothing clicks
        const double smooth = 1.0 - std::exp (-1.0 / (0.006 * sr));
        avS += (av - avS) * smooth;
        ahS += (ah - ahS) * smooth;
        afS += (af - afS) * (smooth * 2.0);

        // voicing source
        double vib = 0.0;
        if (v.vibratoDepth > 0 || s.style == Style::sing)
        {
            const double depth = v.vibratoDepth + (s.style == Style::sing ? 0.25 : 0.0);
            vibPhase += v.vibratoRate / sr;
            const double onset = std::clamp ((double) t / (0.15 * sr), 0.0, 1.0);
            vib = depth * onset * std::sin (2.0 * pi * vibPhase);
        }
        if (n % frame == 0)
            jitterValue = v.jitter * rng.bipolar();
        const double freq = std::clamp (f0 * std::pow (2.0, (semis + vib) / 12.0) * (1.0 + jitterValue), 20.0, sr * 0.2);
        phase += freq / sr;
        if (phase >= 1.0) phase -= std::floor (phase);

        double pulse;
        if (v.buzz > 0)
        {
            const double narrow = phase < 0.08 ? std::sin (pi * phase / 0.08) : 0.0;
            const double open = 0.42, close = 0.16;
            const double smoothPulse = phase < open ? 0.5 * (1.0 - std::cos (pi * phase / open))
                                     : (phase < open + close ? std::cos (0.5 * pi * (phase - open) / close) : 0.0);
            pulse = narrow * v.buzz + smoothPulse * (1.0 - v.buzz);
        }
        else
        {
            const double open = 0.4 + 0.15 * v.tilt, close = 0.12 + 0.1 * v.tilt;
            pulse = phase < open ? 0.5 * (1.0 - std::cos (pi * phase / open))
                  : (phase < open + close ? std::cos (0.5 * pi * (phase - open) / close) : 0.0);
        }
        double source = (pulse - prevPulse) * (sr / (freq * 4.0)); // differentiated pulse (lip radiation), level-normalised
        prevPulse = pulse;
        source = std::clamp (source, -4.0, 4.0) * 0.25;

        const double noise = rng.bipolar();
        double voice = 0.0, asp = 0.0;
        switch (s.voicing)
        {
            case Voicing::normal: voice = avS; asp = ahS + v.breath * avS; break;
            case Voicing::breathy: voice = avS * 0.7; asp = ahS + (0.22 + v.breath) * avS; break;
            case Voicing::whispered: voice = 0.0; asp = ahS + 0.55 * avS; break;
        }
        // breath noise follows the glottal cycle a little
        const double aspiration = noise * asp * (s.voicing == Voicing::whispered ? 1.0 : (0.6 + 0.8 * pulse));
        double x = source * voice + aspiration * 0.5;

        // cascade vocal tract
        if (f1 != lastF1) { r1.set (f1, 60 + 0.05 * f1, sr); lastF1 = f1; }
        if (f2 != lastF2) { r2.set (f2, 80 + 0.04 * f2, sr); lastF2 = f2; }
        if (f3 != lastF3) { r3.set (f3, 140, sr); lastF3 = f3; }
        if (nasal != lastNasal) { nasalZero.set (270 + 330 * nasal, 120, sr); lastNasal = nasal; }
        x = nasalZero.process (nasalPole.process (x));
        x = r5.process (r4.process (r3.process (r2.process (r1.process (x)))));
        if (nasal > 0.5)
            x *= 0.6;

        // parallel frication
        if (fn != lastFn) { fric.set (std::min (fn * std::sqrt (v.formantScale), sr * 0.42), fbw, sr); lastFn = fn; }
        x += fric.process (noise * afS) * 1.2;

        // personality effects
        if (v.ring > 0)
        {
            ringPhase += v.ringHz / sr;
            x = x * (1.0 - v.ring) + x * v.ring * std::sin (2.0 * pi * ringPhase);
        }
        if (v.comb > 0 && comb.size() > 1)
        {
            const double delayed = comb[combPos];
            const double y = x + delayed * v.comb;
            comb[combPos] = y;
            combPos = (combPos + 1) % comb.size();
            x = y * (1.0 - 0.4 * v.comb);
        }

        // remove DC
        dc = x - dcIn + 0.995 * dc;
        dcIn = x;
        out[base + (std::size_t) n] = (float) dc;
        (void) glottalLow;
    }
}

int roundToInt (double v) noexcept { return (int) std::lround (v); }
} // namespace

const char* personalityName (int index) noexcept { return index >= 0 && index < personalityCount ? personalityNames[index] : ""; }
const char* personalityHint (int index) noexcept { return index >= 0 && index < personalityCount ? personalityHints[index] : ""; }

const char* styleName (Style style) noexcept
{
    switch (style)
    {
        case Style::natural: return TR_NOOP ("Natural");
        case Style::monotone: return TR_NOOP ("Monotone");
        case Style::sing: return TR_NOOP ("Sing");
        case Style::random: return TR_NOOP ("Random");
    }
    return "";
}

const char* styleHint (Style style) noexcept
{
    switch (style)
    {
        case Style::natural: return "Natural pitch contour";
        case Style::monotone: return "Flat pitch contour";
        case Style::sing: return "Singing mode";
        case Style::random: return "Major scale random notes per syllable";
    }
    return "";
}

int styleToCode (Style style) noexcept
{
    const int index = (int) style;
    return index + (index > 0 ? 1 : 0);
}

Style styleFromCode (int code) noexcept
{
    const int index = clampInt (code - (code != 0 ? 1 : 0), 0, styleCount - 1);
    return (Style) index;
}

const char* voicingName (Voicing voicing) noexcept
{
    switch (voicing)
    {
        case Voicing::normal: return TR_NOOP ("Normal");
        case Voicing::breathy: return TR_NOOP ("Breathy");
        case Voicing::whispered: return TR_NOOP ("Whispered");
    }
    return "";
}

const char* voicingHint (Voicing voicing) noexcept
{
    switch (voicing)
    {
        case Voicing::normal: return "Normal voicing";
        case Voicing::breathy: return "Mellower with breath noise";
        case Voicing::whispered: return "No voicing, all aspiration";
    }
    return "";
}

Settings personalityDefaults (int personality, const Settings& keepText)
{
    struct D { int pitch, rate; Style style; };
    static const D defaults[personalityCount] = {
        { 110, 150, Style::natural }, { 208, 150, Style::natural }, { 73, 140, Style::natural },
        { 262, 145, Style::natural }, { 46, 140, Style::monotone }, { 185, 140, Style::natural },
        { 98, 140, Style::natural }, { 117, 150, Style::natural }, { 330, 150, Style::natural },
        { 92, 150, Style::monotone }, { 147, 150, Style::monotone }, { 65, 138, Style::monotone },
        { 123, 250, Style::natural }, { 196, 130, Style::natural }, { 370, 150, Style::natural },
        { 277, 140, Style::natural }, { 139, 155, Style::natural }, { 123, 165, Style::natural },
        { 147, 152, Style::random }, { 311, 90, Style::sing } };
    Settings s = keepText;
    s.personality = clampInt (personality, 0, personalityCount - 1);
    const auto& d = defaults[s.personality];
    s.pitch = keyToStoredPitch (std::clamp (hzToKey (d.pitch), minKey, maxKey));
    s.rate = d.rate;
    s.style = d.style;
    s.voicing = Voicing::normal;
    return s;
}

double keyToHz (int key) noexcept { return 440.0 * std::pow (2.0, (key - 57) / 12.0); }
int hzToKey (double hz) noexcept { return hz > 0 ? roundToInt (std::log2 (hz / 440.0) * 12.0 + 57.0) : minKey; }
int keyToStoredPitch (int key) noexcept { return roundToInt (keyToHz (key)); }

namespace
{
std::string trim (std::string_view s)
{
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace ((unsigned char) s[a])) ++a;
    while (b > a && std::isspace ((unsigned char) s[b - 1])) --b;
    return std::string (s.substr (a, b - a));
}

bool equalsNoCase (std::string_view a, std::string_view b)
{
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower ((unsigned char) a[i]) != std::tolower ((unsigned char) b[i]))
            return false;
    return true;
}

// Delphi's StrToIntDef as TIniFile.ReadInteger uses it: the whole value must be an integer (decimal or $hex).
bool toInt (const std::string& v, int& out)
{
    if (v.empty()) return false;
    std::size_t i = 0;
    bool neg = false;
    if (v[i] == '-' || v[i] == '+') { neg = v[i] == '-'; ++i; }
    long long acc = 0;
    if (i < v.size() && v[i] == '$')
    {
        ++i;
        if (i >= v.size()) return false;
        for (; i < v.size(); ++i)
        {
            const int d = std::isdigit ((unsigned char) v[i]) ? v[i] - '0' : (std::isxdigit ((unsigned char) v[i]) ? std::tolower ((unsigned char) v[i]) - 'a' + 10 : -1);
            if (d < 0) return false;
            acc = acc * 16 + d;
            if (acc > 0xffffffffLL) return false;
        }
    }
    else
    {
        if (i >= v.size()) return false;
        for (; i < v.size(); ++i)
        {
            if (! std::isdigit ((unsigned char) v[i])) return false;
            acc = acc * 10 + (v[i] - '0');
            if (acc > 0x7fffffffLL + 1) return false;
        }
    }
    out = (int) (neg ? -acc : acc);
    return true;
}
} // namespace

bool parse (std::string_view fileText, Settings& out)
{
    Settings s; // FL's defaults
    s.text.clear();
    bool inSection = false;
    std::size_t start = 0;
    int styleCode = 2, voicing = 0;
    while (start <= fileText.size())
    {
        auto end = fileText.find ('\n', start);
        if (end == std::string_view::npos) end = fileText.size();
        auto line = fileText.substr (start, end - start);
        if (! line.empty() && line.back() == '\r') line.remove_suffix (1);
        start = end + 1;
        // a UTF-8 byte order mark on the first line
        if (line.size() >= 3 && (unsigned char) line[0] == 0xef && (unsigned char) line[1] == 0xbb && (unsigned char) line[2] == 0xbf)
            line.remove_prefix (3);
        const auto t = trim (line);
        if (t.empty() || t[0] == ';')
            continue;
        if (t.front() == '[')
        {
            const auto close = t.find (']');
            inSection = close != std::string::npos && equalsNoCase (trim (std::string_view (t).substr (1, close - 1)), "Speaker Settings");
            continue;
        }
        if (! inSection)
            continue;
        const auto eq = line.find ('=');
        if (eq == std::string_view::npos)
            continue;
        const auto key = trim (line.substr (0, eq));
        const auto value = std::string (line.substr (eq + 1)); // TIniFile keeps the value's own spaces... trimmed below
        const auto v = trim (value);
        int n = 0;
        if (equalsNoCase (key, "Text"))
            s.text = v;
        else if (equalsNoCase (key, "Style")) { if (toInt (v, n)) styleCode = n; }
        else if (equalsNoCase (key, "Personality")) { if (toInt (v, n)) s.personality = n; }
        else if (equalsNoCase (key, "Pitch")) { if (toInt (v, n)) s.pitch = n; }
        else if (equalsNoCase (key, "Rate")) { if (toInt (v, n)) s.rate = n; }
        else if (equalsNoCase (key, "VoicingMode")) { if (toInt (v, n)) voicing = n; }
        if (end == fileText.size())
            break;
    }
    s.style = styleFromCode (styleCode);
    s.voicing = (Voicing) clampInt (voicing, 0, voicingCount - 1);
    s.personality = clampInt (s.personality, 0, personalityCount - 1);
    out = s;
    return ! s.text.empty();
}

std::string write (const Settings& s)
{
    std::string out = "[Speaker Settings]\r\n";
    out += "Style=" + std::to_string (styleToCode (s.style)) + "\r\n";
    out += "Personality=" + std::to_string (s.personality) + "\r\n";
    out += "Pitch=" + std::to_string (s.pitch) + "\r\n";
    out += "Rate=" + std::to_string (s.rate) + "\r\n";
    out += "VoicingMode=" + std::to_string ((int) s.voicing) + "\r\n";
    out += "Text=" + s.text + "\r\n";
    return out;
}

std::string suggestedFileName (const Settings& s)
{
    auto isAlnum = [] (char c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); };
    auto upper = [] (char c) { return c >= 'a' && c <= 'z' ? (char) (c - 32) : c; };
    std::string t = s.text;
    if (! t.empty())
        t[0] = upper (t[0]);
    std::string kept;
    for (std::size_t i = 0; i < t.size(); ++i)
    {
        const char c = t[i];
        if (isAlnum (c))
            kept += c;
        else if ((c == ' ' || c == '_') && i + 1 < t.size())
            t[i + 1] = upper (t[i + 1]); // the next letter becomes a capital; the separator goes
    }
    if (kept.size() > 32)
        kept.resize (32);
    return kept + "_" + personalityName (clampInt (s.personality, 0, personalityCount - 1)) + ".speech";
}

std::vector<Word> splitWords (std::string_view text)
{
    std::vector<Word> words;
    std::size_t start = 0;
    while (start <= text.size())
    {
        auto end = text.find (' ', start);
        if (end == std::string_view::npos) end = text.size();
        std::string chunk (text.substr (start, end - start));
        start = end + 1;
        // "_" joins words: spoken together, one slice
        std::replace (chunk.begin(), chunk.end(), '_', ' ');
        if (! chunk.empty())
        {
            Word w;
            const auto close = chunk.find (')'), open = chunk.find ('(');
            if (close != std::string::npos && chunk[0] != '(' && open != std::string::npos && open < close && open > 0)
            {
                int n = 0;
                const auto inside = chunk.substr (open + 1, close - open - 1);
                if (! toInt (inside, n)) n = 0; // StrToIntDef (..., 0)
                w.semitones = n;
                chunk = chunk.substr (0, open);
            }
            w.text = chunk;
            if (! w.text.empty())
                words.push_back (std::move (w));
        }
        if (end == text.size())
            break;
    }
    return words;
}

int wordPitchHz (int pitch, int semitones) noexcept
{
    // FL's wrapper: Hz -> nearest semitone above 16.35 Hz (octave, note), plus the word's offset, back to Hz,
    // clamped to [10, the frequency of octave 5 note 11].
    const double p = std::max (1, pitch);
    const double steps = (std::log (p) - std::log (16.35)) / 0.05776226505;
    int note = (int) steps;          // Trunc
    if (steps - note > 0.5)
        ++note;
    note += semitones;
    auto hzOf = [] (int n) { return (double) (float) (std::exp (n * 0.0577622650466621) * 16.35); };
    const double hz = std::clamp (hzOf (note), 10.0, hzOf (5 * 12 + 11));
    return roundToInt (hz);
}

Rendered render (const Settings& settings, double sampleRate)
{
    Rendered r;
    r.sampleRate = sampleRate > 1000 ? sampleRate : 44100.0;
    const auto voice = voiceFor (settings.personality);
    Rng rng (hashText (settings.text) ^ (std::uint32_t) (settings.personality * 7919 + settings.pitch));
    for (const auto& w : splitWords (settings.text))
    {
        r.wordStarts.push_back ((std::int64_t) r.samples.size());
        r.wordNames.push_back (w.text);
        const auto before = r.samples.size();
        synthesizeChunk (w.text, wordPitchHz (settings.pitch, w.semitones), settings, voice, r.sampleRate, rng, r.samples);
        if (r.samples.size() == before) // nothing speakable (punctuation only): a short pause keeps the slice
            r.samples.resize (before + (std::size_t) (0.12 * r.sampleRate), 0.0f);
    }
    float peak = 0.0f;
    for (const auto x : r.samples)
        peak = std::max (peak, std::abs (x));
    if (peak > 1e-9f)
    {
        const float gain = 0.89f / peak; // about -1 dBFS
        for (auto& x : r.samples)
            x *= gain;
    }
    return r;
}
} // namespace lynx::speech
