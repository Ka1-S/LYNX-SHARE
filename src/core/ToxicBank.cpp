// LYNX's own Toxic Biohazard bank (round 168n). Designed for LYNX — never converted from FL's preset files — and
// playing only the operators' own drawn spectra (wave 0, written here from simple harmonic recipes) and the sine
// (wave 1), so every preset works without FL Studio's files. Values follow the parameter laws of REA_rodada45 §2.2.

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>

#include "core/LynxBanks.h"
#include "core/Toxic.h"

namespace lynx::banks
{
namespace
{
constexpr std::size_t spectraAt = 12 + 0x40;

struct Tox
{
    std::vector<std::uint8_t> s = toxicDefaultState();

    void p (int index, float x)
    {
        const auto at = toxicParamOffset (s, index);
        if (at >= 0)
            std::memcpy (s.data() + at, &x, 4);
    }
    // acb (x, −99, 99) = n
    static float steps99 (int n) { return ((float) n + (n < 0 ? -0.25f : 0.25f) + 99.0f) / 198.0f; }
    // T(x) = 50·x⁴ seconds
    static float time (float seconds) { return std::pow (std::max (seconds, 0.0011f) / 50.0f, 0.25f); }

    void op (int o, bool on = true) { p (25 + o, on ? 1.0f : 0.0f); }
    void semis (int o, int n) { p (31 + o, steps99 (n)); }
    void cents (int o, int c) { p (37 + o, steps99 (c)); }
    void fixedPhase (int o, float phase = 0.0f) { p (49 + o, 0.0f), p (55 + o, phase); }
    void velocity (int o, float amount) { p (61 + o, amount); }
    // init / sustain levels linear (their laws are x²), times in seconds
    void env (int o, float attack, float decay, float sustain, float release, float init = 0.0f)
    {
        p (67 + o, std::sqrt (init));
        p (73 + o, time (attack));
        p (79 + o, time (decay));
        p (85 + o, std::sqrt (sustain));
        p (91 + o, time (release));
    }
    void pan (int o, float x) { p (97 + o, x); }
    void mix (int o, float level) { p (103 + o, std::sqrt (level)); }
    // operator `from` modulates `to` with a cell of k/100 (amount 2·(k/100)²)
    void fm (int from, int to, int k) { p (109 + 6 * to + from, ((float) k + 0.5f) / 100.0f); }
    void wave (int slot, int w) { p (306 + slot, (float) w); }
    void lfoToOsc (int lfo, int o, float amount) { p (145 + 6 * lfo + o, (amount * 100.0f + 99.0f + 0.5f) / 199.0f); }
    // a drawn spectrum (wave 0) for operator / LFO slot: amplitude of harmonic h (1…128), phases 0
    void spectrum (int slot, const std::function<float (int)>& amp)
    {
        const auto at = spectraAt + 1024 * (std::size_t) slot;
        for (int h = 1; h <= 128; ++h)
        {
            const auto a = amp (h), phase = 0.0f;
            std::memcpy (s.data() + at + 4 * (std::size_t) (h - 1), &a, 4);
            std::memcpy (s.data() + at + 512 + 4 * (std::size_t) (h - 1), &phase, 4);
        }
        wave (slot, 0);
    }
    void master (float attack, float decay, float sustain, float release)
    {
        p (3, time (attack));
        p (4, time (decay));
        p (5, std::sqrt (sustain));
        p (6, time (std::max (0.0f, (release - 0.02f) / 0.35f)));
    }
    void gain (float x) { p (0, x); }
    void drive (float x) { p (1, x); }
    void transpose (int semis) { p (2, ((float) semis + 24.0f + (semis < 0 ? -0.25f : 0.25f)) / 48.0f); }
    void velocityCurve (float x) { p (7, x); }
    void unison (int voices, float detune, float spread) { p (302, (float) (voices - 1)), p (8, detune), p (9, spread); }
    void poly (int voices) { p (301, (float) voices); }
    void mono (float glideSeconds, int mode = 0) { p (301, 0.0f), p (10, std::cbrt (glideSeconds)), p (303, (float) mode); }
    // filter: 1 LP, 2 BP, 3 HP; env amount −1…1 (law sign·(2x − 1)²)
    void filter (int mode, float cutoff, float resonance, float envAmount = 0.0f, float attack = 0.001f, float decay = 0.5f,
                 float sustain = 1.0f, float release = 0.3f, float keyTrack = 0.5f)
    {
        p (300, (float) mode);
        p (15, cutoff), p (16, resonance), p (17, keyTrack);
        p (23, 0.5f + (envAmount < 0 ? -0.5f : 0.5f) * std::sqrt (std::fabs (envAmount)));
        p (19, time (attack)), p (20, time (decay)), p (21, std::sqrt (sustain)), p (22, time (release));
    }
    void lfo (int l, float speed01, int sync = 0) { p (13 + l, speed01), p (296 + 2 * l, (float) sync); }
    void lfoToCutoff (float x) { p (24, x); }
    void lfo2ToPitch (float x) { p (12, x); }
    // effect slot (0 delay, 1 chorus, 2 reverb, 3 flanger, 4 phaser, 5 lo-fi); fields by their parameter offset
    void fx (int slot, int type, std::initializer_list<std::pair<int, float>> fields)
    {
        p (236 + slot, 1.0f);
        p (314 + slot, (float) type);
        for (const auto& [first, value] : fields)
            p (first + slot, value);
    }
    void reverb (int slot, float mix, float decay, float damp = 0.4f, float hiCut = 0.3f) { fx (slot, 2, { { 258, mix }, { 260, decay }, { 262, damp }, { 264, hiCut } }); }
    void delay (int slot, float time, float feedback, float mix, int sync = 0)
    {
        fx (slot, 0, { { 266, time }, { 268, feedback }, { 270, 0.2f }, { 272, 0.1f }, { 274, 0.3f }, { 276, mix } });
        p (304 + slot, (float) sync);
    }
    void chorus (int slot, float depth, float rate, float mix) { fx (slot, 1, { { 252, depth }, { 254, rate }, { 256, mix } }); }
    void eq (std::initializer_list<float> bands) // dB −18…18 per band, 62 Hz … 8 kHz
    {
        p (227, 1.0f);
        int b = 0;
        for (const auto db : bands)
        {
            // gain = ±y²·18 with y = 2v − 1
            const auto y = std::sqrt (std::min (1.0f, std::fabs (db) / 18.0f)) * (db < 0 ? -1.0f : 1.0f);
            p (228 + b++, (y + 1.0f) * 0.5f);
        }
    }
};

float saw (int h) { return 1.0f / (float) h; }
float square (int h) { return h % 2 == 1 ? 1.0f / (float) h : 0.0f; }
float soft (int h) { return 1.0f / (float) (h * h); }
float organ (int h) { return h == 1 ? 1.0f : h == 2 ? 0.8f : h == 3 ? 0.6f : h == 4 ? 0.5f : h == 6 ? 0.35f : h == 8 ? 0.3f : 0.0f; }
float hollow (int h) { return h % 2 == 1 ? 1.0f / (float) (h * h) * (h == 3 ? 3.0f : 1.0f) : 0.0f; }
float vocal (int h) // two formant humps (about "ah" on C3)
{
    const auto f = (float) h;
    return (std::exp (-std::pow ((f - 5.5f) / 2.2f, 2.0f)) + 0.6f * std::exp (-std::pow ((f - 9.5f) / 2.5f, 2.0f)) + 0.25f / f) / std::sqrt (f);
}
float bell (int h) { return h == 1 ? 1.0f : h == 3 ? 0.4f : h == 5 ? 0.25f : h == 9 ? 0.15f : 0.0f; }

using Recipe = void (*) (Tox&);

Preset make (const char* name, const char* category, std::vector<std::string> tags, Recipe recipe)
{
    return { name, category, std::move (tags), [recipe] (ChannelDef& c) {
                Tox t;
                recipe (t);
                c.pluginState = t.s;
            } };
}
} // namespace

std::vector<Preset> toxicBank()
{
    return {
        make ("Init", "Init", { "init", "sine" }, [] (Tox&) {}),

        // --- bass
        make ("FM Sub Bass", "Bass", { "fm", "sub", "clean" }, [] (Tox& t) {
            t.poly (1), t.mono (0.03f);
            t.fixedPhase (0), t.fixedPhase (1);
            t.op (1), t.semis (1, 0), t.mix (1, 0.0f), t.env (1, 0.001f, 0.25f, 0.15f, 0.1f, 0.6f);
            t.fm (1, 0, 22);
            t.env (0, 0.002f, 1.0f, 1.0f, 0.08f);
            t.master (0.002f, 1.0f, 1.0f, 0.08f);
            t.gain (0.55f);
        }),
        make ("Growl Bass", "Bass", { "fm", "dirty", "dubstep" }, [] (Tox& t) {
            t.poly (1), t.mono (0.05f);
            t.spectrum (0, saw);
            t.op (1), t.semis (1, 0), t.cents (1, 7), t.mix (1, 0.0f), t.env (1, 0.001f, 0.6f, 0.5f, 0.1f, 1.0f);
            t.fm (1, 0, 35), t.fm (1, 1, 18);
            t.lfo (0, 0.35f, 4), t.lfoToOsc (0, 1, 0.8f);
            t.filter (1, 0.62f, 0.35f, 0.3f, 0.001f, 0.4f, 0.4f, 0.1f);
            t.drive (0.35f);
            t.master (0.002f, 1.0f, 1.0f, 0.06f);
            t.gain (0.45f);
        }),
        make ("Rubber Bass", "Bass", { "pluck", "funk" }, [] (Tox& t) {
            t.poly (1), t.mono (0.0f);
            t.spectrum (0, square);
            t.filter (1, 0.32f, 0.55f, 0.55f, 0.001f, 0.22f, 0.1f, 0.1f);
            t.master (0.001f, 0.6f, 0.5f, 0.06f);
            t.gain (0.5f);
        }),
        make ("Saw Reese", "Bass", { "reese", "unison", "dnb" }, [] (Tox& t) {
            t.poly (1), t.mono (0.06f);
            t.spectrum (0, saw);
            t.unison (4, 0.35f, 0.5f);
            t.filter (1, 0.55f, 0.2f);
            t.master (0.003f, 1.0f, 1.0f, 0.1f);
            t.chorus (0, 0.3f, 0.15f, 0.25f);
            t.gain (0.42f);
        }),
        make ("Wobble Bass", "Bass", { "wobble", "lfo" }, [] (Tox& t) {
            t.poly (1), t.mono (0.04f);
            t.spectrum (0, saw);
            t.op (1), t.wave (1, 1), t.semis (1, -12), t.mix (1, 0.5f);
            t.filter (1, 0.35f, 0.5f);
            t.lfo (0, 0.4f, 4), t.lfoToCutoff (0.75f);
            t.master (0.002f, 1.0f, 1.0f, 0.08f);
            t.gain (0.45f);
        }),
        make ("Deep Pluck Bass", "Bass", { "fm", "pluck", "deep" }, [] (Tox& t) {
            t.poly (1), t.mono (0.0f);
            t.fixedPhase (0), t.fixedPhase (1);
            t.op (1), t.semis (1, 12), t.mix (1, 0.0f), t.env (1, 0.001f, 0.12f, 0.0f, 0.05f, 1.0f);
            t.fm (1, 0, 45);
            t.env (0, 0.001f, 0.9f, 0.3f, 0.1f);
            t.master (0.001f, 1.0f, 1.0f, 0.1f);
            t.gain (0.55f);
        }),

        // --- lead
        make ("Toxic Lead", "Lead", { "fm", "bright" }, [] (Tox& t) {
            t.poly (1), t.mono (0.06f);
            t.spectrum (0, saw);
            t.op (1), t.semis (1, 7), t.mix (1, 0.0f), t.env (1, 0.01f, 0.8f, 0.4f, 0.2f);
            t.fm (1, 0, 20);
            t.lfo (1, 0.3f), t.lfo2ToPitch (0.12f);
            t.filter (1, 0.75f, 0.25f, 0.2f, 0.001f, 0.6f, 0.6f, 0.2f);
            t.delay (0, 0.4f, 0.35f, 0.25f, 4);
            t.master (0.005f, 1.0f, 1.0f, 0.2f);
            t.gain (0.42f);
        }),
        make ("Square Lead", "Lead", { "chip", "square" }, [] (Tox& t) {
            t.poly (1), t.mono (0.03f);
            t.spectrum (0, square);
            t.lfo (1, 0.33f), t.lfo2ToPitch (0.1f);
            t.master (0.002f, 1.0f, 1.0f, 0.1f);
            t.delay (0, 0.3f, 0.3f, 0.2f, 4);
            t.gain (0.42f);
        }),
        make ("Supersaw Lead", "Lead", { "unison", "trance" }, [] (Tox& t) {
            t.poly (6);
            t.spectrum (0, saw);
            t.unison (7, 0.4f, 0.9f);
            t.filter (1, 0.85f, 0.1f);
            t.reverb (0, 0.25f, 0.5f);
            t.delay (1, 0.4f, 0.3f, 0.18f, 4);
            t.master (0.004f, 1.0f, 1.0f, 0.25f);
            t.gain (0.36f);
        }),
        make ("Vocal Lead", "Lead", { "formant", "voice" }, [] (Tox& t) {
            t.poly (1), t.mono (0.08f);
            t.spectrum (0, vocal);
            t.lfo (1, 0.3f), t.lfo2ToPitch (0.15f);
            t.reverb (0, 0.2f, 0.45f);
            t.master (0.03f, 1.0f, 1.0f, 0.25f);
            t.gain (0.5f);
        }),
        make ("Screamer", "Lead", { "fm", "distorted" }, [] (Tox& t) {
            t.poly (1), t.mono (0.04f);
            t.op (1), t.semis (1, 12), t.mix (1, 0.0f), t.fm (1, 0, 40), t.fm (1, 1, 30);
            t.drive (0.5f);
            t.filter (1, 0.7f, 0.3f);
            t.master (0.002f, 1.0f, 1.0f, 0.15f);
            t.gain (0.38f);
        }),

        // --- pad
        make ("Warm Pad", "Pad", { "soft", "unison" }, [] (Tox& t) {
            t.poly (16);
            t.spectrum (0, soft);
            t.unison (3, 0.3f, 0.8f);
            t.filter (1, 0.55f, 0.1f, 0.15f, 0.8f, 1.5f, 0.6f, 1.0f);
            t.reverb (0, 0.4f, 0.7f);
            t.master (0.6f, 1.0f, 1.0f, 1.2f);
            t.gain (0.42f);
        }),
        make ("Glass Pad", "Pad", { "fm", "airy" }, [] (Tox& t) {
            t.poly (16);
            t.op (1), t.semis (1, 19), t.mix (1, 0.15f), t.fm (1, 0, 15);
            t.op (2), t.semis (2, 12), t.cents (2, 8), t.mix (2, 0.4f);
            t.lfo (0, 0.15f), t.lfoToOsc (0, 1, 0.5f);
            t.reverb (0, 0.5f, 0.8f, 0.2f, 0.1f);
            t.chorus (1, 0.4f, 0.2f, 0.3f);
            t.master (0.8f, 1.0f, 1.0f, 1.5f);
            t.gain (0.45f);
        }),
        make ("Organ Pad", "Pad", { "organ", "drawbar" }, [] (Tox& t) {
            t.poly (16);
            t.spectrum (0, organ);
            t.chorus (0, 0.35f, 0.3f, 0.4f);
            t.reverb (1, 0.3f, 0.6f);
            t.master (0.15f, 1.0f, 1.0f, 0.6f);
            t.gain (0.45f);
        }),
        make ("Dark Choir", "Pad", { "formant", "choir", "dark" }, [] (Tox& t) {
            t.poly (16);
            t.spectrum (0, vocal);
            t.unison (4, 0.25f, 0.9f);
            t.filter (1, 0.45f, 0.05f);
            t.reverb (0, 0.5f, 0.85f);
            t.master (0.9f, 1.0f, 1.0f, 1.6f);
            t.gain (0.45f);
        }),

        // --- pluck
        make ("FM Pluck", "Pluck", { "fm", "short" }, [] (Tox& t) {
            t.poly (12);
            t.op (1), t.semis (1, 12), t.mix (1, 0.0f), t.env (1, 0.001f, 0.15f, 0.0f, 0.1f, 1.0f), t.fm (1, 0, 35);
            t.env (0, 0.001f, 0.6f, 0.0f, 0.2f);
            t.delay (0, 0.4f, 0.4f, 0.2f, 4);
            t.master (0.001f, 1.0f, 1.0f, 0.3f);
            t.gain (0.5f);
        }),
        make ("Saw Pluck", "Pluck", { "saw", "filter" }, [] (Tox& t) {
            t.poly (12);
            t.spectrum (0, saw);
            t.unison (2, 0.2f, 0.6f);
            t.filter (1, 0.3f, 0.3f, 0.6f, 0.001f, 0.25f, 0.0f, 0.2f);
            t.reverb (0, 0.25f, 0.5f);
            t.master (0.001f, 0.5f, 0.0f, 0.25f);
            t.gain (0.45f);
        }),
        make ("Kalimba", "Pluck", { "fm", "mallet" }, [] (Tox& t) {
            t.poly (12);
            t.fixedPhase (0), t.fixedPhase (1);
            t.op (1), t.semis (1, 41), t.cents (1, -14), t.mix (1, 0.0f), t.env (1, 0.001f, 0.05f, 0.0f, 0.05f, 1.0f), t.fm (1, 0, 30);
            t.env (0, 0.001f, 0.9f, 0.0f, 0.4f);
            t.reverb (0, 0.2f, 0.4f);
            t.master (0.001f, 1.0f, 1.0f, 0.5f);
            t.gain (0.55f);
        }),
        make ("Harp Pluck", "Pluck", { "soft", "harp" }, [] (Tox& t) {
            t.poly (16);
            t.spectrum (0, hollow);
            t.env (0, 0.002f, 1.4f, 0.0f, 0.6f);
            t.reverb (0, 0.3f, 0.6f);
            t.master (0.002f, 1.0f, 1.0f, 0.7f);
            t.gain (0.55f);
        }),

        // --- keys
        make ("E-Piano", "Keys", { "fm", "rhodes" }, [] (Tox& t) {
            t.poly (16);
            t.fixedPhase (0), t.fixedPhase (1), t.fixedPhase (2), t.fixedPhase (3);
            t.op (1), t.semis (1, 0), t.mix (1, 0.0f), t.env (1, 0.001f, 0.8f, 0.1f, 0.3f, 0.8f), t.fm (1, 0, 25), t.velocity (1, 1.0f);
            t.op (2), t.semis (2, 0), t.cents (2, 3), t.mix (2, 0.5f), t.env (2, 0.001f, 2.0f, 0.2f, 0.4f);
            t.op (3), t.semis (3, 14), t.mix (3, 0.0f), t.env (3, 0.001f, 0.08f, 0.0f, 0.05f, 1.0f), t.fm (3, 2, 30);
            t.env (0, 0.001f, 2.5f, 0.2f, 0.4f);
            t.chorus (0, 0.25f, 0.3f, 0.3f);
            t.master (0.001f, 1.0f, 1.0f, 0.4f);
            t.gain (0.45f);
        }),
        make ("DX Bell Keys", "Keys", { "fm", "bell" }, [] (Tox& t) {
            t.poly (16);
            t.fixedPhase (0), t.fixedPhase (1);
            t.op (1), t.semis (1, 27), t.cents (1, 7), t.mix (1, 0.0f), t.env (1, 0.001f, 1.2f, 0.0f, 0.5f, 1.0f), t.fm (1, 0, 28);
            t.env (0, 0.001f, 2.2f, 0.0f, 0.8f);
            t.reverb (0, 0.3f, 0.6f);
            t.master (0.001f, 1.0f, 1.0f, 0.8f);
            t.gain (0.5f);
        }),
        make ("Drawbar Organ", "Keys", { "organ", "percussive" }, [] (Tox& t) {
            t.poly (16);
            t.spectrum (0, organ);
            t.op (1), t.semis (1, 24), t.mix (1, 0.3f), t.env (1, 0.001f, 0.25f, 0.0f, 0.05f);
            t.chorus (0, 0.4f, 0.55f, 0.45f);
            t.master (0.003f, 1.0f, 1.0f, 0.06f);
            t.gain (0.42f);
        }),
        make ("Clavinet", "Keys", { "funk", "pluck" }, [] (Tox& t) {
            t.poly (12);
            t.spectrum (0, square);
            t.filter (3, 0.25f, 0.1f);
            t.env (0, 0.001f, 0.7f, 0.15f, 0.05f);
            t.master (0.001f, 1.0f, 1.0f, 0.06f);
            t.gain (0.5f);
        }),

        // --- fx
        make ("Bomb Drop", "FX", { "fm", "riser", "noise" }, [] (Tox& t) {
            t.poly (2);
            t.op (1), t.semis (1, -5), t.mix (1, 0.0f), t.fm (1, 0, 60), t.fm (1, 1, 40);
            t.filter (1, 0.85f, 0.4f, -0.8f, 0.001f, 3.0f, 0.0f, 1.0f);
            t.reverb (0, 0.4f, 0.8f);
            t.master (0.01f, 3.0f, 0.0f, 1.0f);
            t.gain (0.42f);
        }),
        make ("Laser Zap", "FX", { "zap", "pitch" }, [] (Tox& t) {
            t.poly (4);
            t.spectrum (0, square);
            t.filter (1, 0.4f, 0.6f, 0.7f, 0.001f, 0.15f, 0.0f, 0.1f);
            t.p (11, 0.9f); // filter envelope to pitch: +19 semitones per unit of envelope
            t.delay (0, 0.25f, 0.45f, 0.3f, 3);
            t.master (0.001f, 0.25f, 0.0f, 0.1f);
            t.gain (0.45f);
        }),
        make ("Alien Texture", "FX", { "atmosphere", "lfo" }, [] (Tox& t) {
            t.poly (8);
            t.op (1), t.semis (1, 5), t.mix (1, 0.0f), t.fm (1, 0, 30);
            t.lfo (0, 0.12f), t.lfoToOsc (0, 1, 1.0f);
            t.lfo (1, 0.25f), t.lfo2ToPitch (0.3f);
            t.fx (0, 4, { { 284, 0.4f }, { 286, 0.2f }, { 288, 0.6f }, { 290, 0.5f }, { 292, 0.7f } });
            t.reverb (1, 0.5f, 0.85f);
            t.master (1.0f, 1.0f, 1.0f, 2.0f);
            t.gain (0.45f);
        }),
    };
}
} // namespace lynx::banks
