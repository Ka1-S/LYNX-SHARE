// LYNX's own PoiZone bank (round 168n). Designed for LYNX — never converted from FL's preset files. PoiZone's engine
// is self-contained (two naive saw / pulse oscillators, noise, a 4-pole filter, two envelopes, an LFO, delay, chorus,
// gate and arpeggiator; REA_rodada44), so these presets play without FL Studio. Values follow the laws of §1.3.

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/LynxBanks.h"
#include "core/PoiZone.h"

namespace lynx::banks
{
namespace
{
struct Poi
{
    std::vector<std::uint8_t> s = poizoneDefaultState();

    void p (int index, float x)
    {
        const auto at = poizoneParamOffset (s, index);
        if (at >= 0)
            std::memcpy (s.data() + at, &x, 4);
    }
    // −24 − trunc(x·(−48)) = n
    static float semis (int n) { return ((float) n + 24.0f + 0.25f) / 48.0f; }
    // clamp(x⁴·(hi − lo) + lo) milliseconds
    static float ms (float milliseconds, float lo, float hi) { return std::pow (std::clamp ((milliseconds - lo) / (hi - lo), 0.0f, 1.0f), 0.25f); }
    static float sel (int i, int count) { return (float) i / (float) (count - 1); }

    void oscA (bool pulse, float width = 1.0f) { p (0, pulse ? 1.0f : 0.0f), p (1, width); }
    void oscB (bool pulse, int semitones, float detune = 0.0f, float width = 1.0f)
    {
        p (4, pulse ? 1.0f : 0.0f), p (5, width), p (6, semis (semitones)), p (7, detune);
    }
    void balance (float x) { p (8, x); }            // 0 = osc A … 1 = osc B
    void noise (float x) { p (9, x); }
    void sync (bool on) { p (2, on ? 1.0f : 0.0f); }
    void ring (bool on) { p (3, on ? 1.0f : 0.0f); }
    // filter: 0 LP, 1 BP, 2 HP
    void filter (int mode, float cutoff, float resonance, float keyTrack = 0.5f, float velTrack = 0.0f)
    {
        p (66, sel (mode, 3)), p (18, cutoff), p (19, resonance), p (20, keyTrack), p (21, velTrack);
    }
    // modulation envelope: destination 0 cutoff, 1 Frq A, 2 Frq AB, 3 PW A, 4 PW AB; amount −1…1; times in ms
    void modEnv (int destination, float amount, float attack, float decay, float sustain, float release)
    {
        p (65, sel (destination, 5)), p (10, (amount + 1.0f) * 0.5f);
        p (11, ms (attack, 5, 50000)), p (12, ms (decay, 5, 50000)), p (13, sustain), p (14, ms (release, 5, 50000));
    }
    void amp (float attack, float decay, float sustain, float release)
    {
        p (22, ms (attack, 1, 5000)), p (23, ms (decay, 5, 20000)), p (24, std::sqrt (sustain)), p (25, ms (release, 15, 10000));
    }
    // LFO: destination 0 cutoff, 1 Frq A, 2 Frq AB, 3 PW A, 4 PW AB, 5 pan; shape 0 saw, 1 pulse, 2 tri, 3 sine, 4 random
    void lfo (int destination, int shape, float amount, float hz, int sync = 0, bool retrigger = false)
    {
        p (67, sel (destination, 6)), p (68, sel (shape, 5)), p (26, (amount + 1.0f) * 0.5f);
        p (28, std::pow (std::clamp (hz / 30.0f, 0.0f, 1.0f), 0.2f)), p (80, (float) sync), p (27, retrigger ? 1.0f : 0.0f);
    }
    void unison (int index, float detune, float pan) { p (75, (float) index), p (29, detune), p (30, pan); } // index 0…6 → 1, 2, 3, 4, 8, 12, 16
    void mono (float glideSeconds, bool held = false)
    {
        p (44, 1.0f), p (46, std::cbrt (std::clamp (glideSeconds / 2.0f, 0.0f, 1.0f))), p (72, held ? 1.0f : 0.0f);
    }
    void octaver() { p (45, 1.0f); }
    void gain (float x) { p (15, x); }
    void transpose (int semitones) { p (17, semis (semitones)); }
    void delay (float timeL, float timeR, float feedback, float wet, float hiCut = 0.6f)
    {
        p (31, 1.0f), p (32, timeL), p (33, timeR), p (34, feedback), p (38, wet), p (37, hiCut);
    }
    void chorus (float rate, float depth, float wet) { p (39, 1.0f), p (40, rate), p (41, depth), p (42, wet); }
    // arpeggiator: mode 1 up, 2 down, 3 up-down, 4 random; range 1…4 octaves; speed index 0…6 → 0.5, 1, 2, 3, 4, 8, 16
    void arp (int mode, int range, int speed) { p (69, sel (mode, 5)), p (70, sel (range - 1, 4)), p (77, (float) speed); }
    // gate: 16 steps (1 = open), speed index, mix, smooth
    void gate (const char* steps, int speed, float mix, float smooth)
    {
        for (int i = 0; i < 16; ++i)
            p (49 + i, steps[i] == 'x' ? 1.0f : 0.0f);
        p (79, (float) speed), p (47, mix), p (48, smooth);
    }
};

using Recipe = void (*) (Poi&);

Preset make (const char* name, const char* category, std::vector<std::string> tags, Recipe recipe)
{
    return { name, category, std::move (tags), [recipe] (ChannelDef& c) {
                Poi t;
                recipe (t);
                c.pluginState = t.s;
            } };
}
} // namespace

std::vector<Preset> poizoneBank()
{
    return {
        make ("Init", "Init", { "init", "saw" }, [] (Poi&) {}),

        // --- bass
        make ("Acid Line", "Bass", { "acid", "303", "resonant" }, [] (Poi& t) {
            t.mono (0.06f), t.oscA (false);
            t.filter (0, 0.25f, 0.75f, 0.3f);
            t.modEnv (0, 0.55f, 5, 220, 0.0f, 120);
            t.amp (1, 400, 0.7f, 40);
            t.gain (0.62f);
        }),
        make ("Fat Saw Bass", "Bass", { "unison", "saw" }, [] (Poi& t) {
            t.mono (0.03f), t.oscA (false), t.oscB (false, -12, 0.08f), t.balance (0.45f);
            t.unison (1, 0.12f, 0.3f);
            t.filter (0, 0.45f, 0.2f);
            t.modEnv (0, 0.3f, 5, 300, 0.2f, 100);
            t.amp (1, 500, 1.0f, 50);
            t.gain (0.58f);
        }),
        make ("Square Sub", "Bass", { "sub", "square" }, [] (Poi& t) {
            t.mono (0.02f), t.oscA (true, 0.5f), t.oscB (false, -12), t.balance (0.3f);
            t.filter (0, 0.3f, 0.05f, 0.4f);
            t.amp (2, 400, 1.0f, 60);
            t.gain (0.6f);
        }),
        make ("Sync Bass", "Bass", { "sync", "aggressive" }, [] (Poi& t) {
            t.mono (0.04f), t.oscA (false), t.oscB (false, 7), t.sync (true), t.balance (0.6f);
            t.modEnv (2, 0.45f, 5, 250, 0.1f, 80);
            t.filter (0, 0.6f, 0.25f);
            t.amp (1, 600, 0.9f, 50);
            t.gain (0.55f);
        }),
        make ("Pluck Bass", "Bass", { "pluck", "short" }, [] (Poi& t) {
            t.oscA (true, 0.7f), t.oscB (false, 0, 0.1f), t.balance (0.5f);
            t.filter (0, 0.2f, 0.4f, 0.4f);
            t.modEnv (0, 0.5f, 5, 160, 0.0f, 80);
            t.amp (1, 300, 0.0f, 80);
            t.gain (0.887f);
        }),

        // --- lead
        make ("Poison Lead", "Lead", { "saw", "classic" }, [] (Poi& t) {
            t.mono (0.08f), t.oscA (false), t.oscB (false, 0, 0.12f), t.balance (0.5f);
            t.filter (0, 0.65f, 0.3f);
            t.modEnv (0, 0.25f, 10, 400, 0.4f, 200);
            t.lfo (1, 3, 0.04f, 5.5f);
            t.amp (3, 800, 1.0f, 150);
            t.delay (0.43f, 0.43f, 0.35f, 0.22f);
            t.gain (0.55f);
        }),
        make ("Square Lead", "Lead", { "square", "pwm" }, [] (Poi& t) {
            t.mono (0.05f), t.oscA (true, 0.5f), t.oscB (true, 12, 0.05f, 0.35f), t.balance (0.35f);
            t.lfo (4, 2, 0.35f, 2.0f);
            t.filter (0, 0.7f, 0.15f);
            t.amp (3, 600, 1.0f, 120);
            t.chorus (0.3f, 0.4f, 0.25f);
            t.gain (0.55f);
        }),
        make ("Supersaw", "Lead", { "unison", "trance" }, [] (Poi& t) {
            t.oscA (false), t.oscB (false, 0, 0.1f), t.balance (0.5f);
            t.unison (4, 0.35f, 0.9f);
            t.filter (0, 0.8f, 0.1f);
            t.amp (5, 800, 1.0f, 300);
            t.delay (0.43f, 0.43f, 0.3f, 0.2f);
            t.chorus (0.25f, 0.4f, 0.3f);
            t.gain (0.307f);
        }),
        make ("Sync Scream", "Lead", { "sync", "sweep" }, [] (Poi& t) {
            t.mono (0.06f), t.oscA (false), t.oscB (false, 12), t.sync (true), t.balance (0.7f);
            t.lfo (2, 2, 0.3f, 0.8f);
            t.filter (0, 0.75f, 0.2f);
            t.amp (3, 600, 1.0f, 150);
            t.gain (0.5f);
        }),
        make ("Octave Lead", "Lead", { "octaver", "unison" }, [] (Poi& t) {
            t.mono (0.07f), t.oscA (true, 0.6f), t.balance (0.0f);
            t.unison (1, 0.06f, 0.5f), t.octaver();
            t.filter (0, 0.6f, 0.2f);
            t.amp (3, 600, 1.0f, 150);
            t.delay (0.29f, 0.43f, 0.3f, 0.2f);
            t.gain (0.55f);
        }),

        // --- pad
        make ("Soft Pad", "Pad", { "warm", "slow" }, [] (Poi& t) {
            t.oscA (false), t.oscB (false, 0, 0.15f), t.balance (0.5f);
            t.unison (2, 0.2f, 0.8f);
            t.filter (0, 0.4f, 0.1f);
            t.modEnv (0, 0.2f, 1200, 2000, 0.5f, 1500);
            t.amp (900, 2000, 1.0f, 1800);
            t.chorus (0.2f, 0.5f, 0.4f);
            t.gain (0.327f);
        }),
        make ("PWM Strings", "Pad", { "pwm", "strings" }, [] (Poi& t) {
            t.oscA (true, 0.55f), t.oscB (true, 0, 0.1f, 0.45f), t.balance (0.5f);
            t.lfo (4, 2, 0.3f, 0.7f);
            t.filter (0, 0.55f, 0.05f);
            t.amp (500, 1000, 1.0f, 900);
            t.chorus (0.25f, 0.5f, 0.4f);
            t.gain (0.387f);
        }),
        make ("Sweep Pad", "Pad", { "sweep", "lfo" }, [] (Poi& t) {
            t.oscA (false), t.oscB (false, 7, 0.1f), t.balance (0.45f);
            t.unison (1, 0.15f, 0.7f);
            t.filter (0, 0.35f, 0.45f);
            t.lfo (0, 2, 0.4f, 0.15f);
            t.amp (1200, 2000, 1.0f, 2000);
            t.delay (0.57f, 0.71f, 0.4f, 0.25f);
            t.gain (0.5f);
        }),
        make ("Noise Wind", "Pad", { "noise", "atmosphere" }, [] (Poi& t) {
            t.noise (0.9f), t.balance (0.5f);
            t.filter (1, 0.45f, 0.6f, 0.0f);
            t.lfo (0, 4, 0.35f, 0.4f);
            t.amp (1500, 2000, 1.0f, 2500);
            t.chorus (0.2f, 0.6f, 0.4f);
            t.gain (0.55f);
        }),

        // --- pluck
        make ("Poly Pluck", "Pluck", { "short", "bright" }, [] (Poi& t) {
            t.oscA (false), t.oscB (false, 12, 0.08f), t.balance (0.4f);
            t.filter (0, 0.3f, 0.3f);
            t.modEnv (0, 0.5f, 5, 200, 0.0f, 150);
            t.amp (1, 450, 0.0f, 200);
            t.delay (0.43f, 0.57f, 0.35f, 0.22f);
            t.gain (0.58f);
        }),
        make ("Arp Pluck", "Pluck", { "arpeggio", "trance" }, [] (Poi& t) {
            t.oscA (false), t.oscB (true, 0, 0.08f, 0.5f), t.balance (0.5f);
            t.filter (0, 0.35f, 0.35f);
            t.modEnv (0, 0.4f, 5, 150, 0.0f, 100);
            t.amp (1, 250, 0.0f, 120);
            t.arp (3, 2, 3);
            t.delay (0.43f, 0.43f, 0.4f, 0.25f);
            t.gain (0.58f);
        }),
        make ("Gated Chords", "Pluck", { "gate", "trance" }, [] (Poi& t) {
            t.oscA (false), t.oscB (false, 0, 0.12f), t.balance (0.5f);
            t.unison (2, 0.2f, 0.8f);
            t.filter (0, 0.55f, 0.15f);
            t.amp (5, 800, 1.0f, 200);
            t.gate ("x.xx.x.xx.xx.x.x", 3, 1.0f, 0.15f);
            t.gain (0.52f);
        }),

        // --- keys
        make ("Funk Key", "Keys", { "clav", "funk" }, [] (Poi& t) {
            t.oscA (true, 0.3f), t.oscB (true, 12, 0.03f, 0.6f), t.balance (0.4f);
            t.filter (0, 0.35f, 0.25f, 0.5f, 0.5f);
            t.modEnv (0, 0.45f, 5, 180, 0.1f, 60);
            t.amp (1, 400, 0.3f, 60);
            t.gain (0.6f);
        }),
        make ("Ring Bell", "Keys", { "ring", "bell" }, [] (Poi& t) {
            t.oscA (false), t.oscB (false, 19, 0.0f), t.ring (true), t.balance (0.5f);
            t.filter (0, 0.75f, 0.1f);
            t.amp (1, 1500, 0.0f, 900);
            t.delay (0.43f, 0.57f, 0.3f, 0.2f);
            t.gain (0.6f);
        }),
        make ("Soft Keys", "Keys", { "mellow" }, [] (Poi& t) {
            t.oscA (true, 0.5f), t.balance (0.0f);
            t.filter (0, 0.3f, 0.05f, 0.6f, 0.6f);
            t.modEnv (0, 0.25f, 5, 600, 0.2f, 300);
            t.amp (2, 1200, 0.4f, 400);
            t.chorus (0.25f, 0.4f, 0.3f);
            t.gain (0.457f);
        }),

        // --- fx
        make ("Sci-Fi Siren", "FX", { "siren", "lfo" }, [] (Poi& t) {
            t.mono (0.0f), t.oscA (true, 0.5f);
            t.lfo (2, 2, 0.6f, 1.5f);
            t.filter (0, 0.7f, 0.2f);
            t.amp (50, 1000, 1.0f, 600);
            t.delay (0.43f, 0.57f, 0.5f, 0.3f);
            t.gain (0.5f);
        }),
        make ("Noise Riser", "FX", { "riser", "noise", "sweep" }, [] (Poi& t) {
            t.noise (1.0f);
            t.filter (0, 0.1f, 0.5f, 0.0f);
            t.modEnv (0, 0.85f, 4000, 1000, 1.0f, 500);
            t.amp (3000, 1000, 1.0f, 600);
            t.gain (0.55f);
        }),
        make ("Zap", "FX", { "zap", "laser" }, [] (Poi& t) {
            t.oscA (true, 0.5f);
            t.modEnv (1, -0.9f, 5, 120, 0.0f, 60);
            t.amp (1, 200, 0.0f, 80);
            t.delay (0.29f, 0.43f, 0.45f, 0.3f);
            t.gain (0.55f);
        }),
    };
}
} // namespace lynx::banks
