#pragma once

// The raw range of every automatable parameter of FL's drum and physical-model generators (round 168p), by the
// engine's own parameter index: the Min / Max of the control in the plugin's FL editor form (REA_rodada168p.md §1).
// Header-only so the engines (and the stand-alone render tools that link one engine) map an automation value 0…1 to
// the raw value without the editor tables of core/DrumControls (whose tables the tests check against these).

#include <cmath>

namespace lynx::drumranges
{
struct Range
{
    bool valid = false;
    float low = 0.0f, high = 1.0f;
    bool integer = false;

    // automation value 0…1 → raw value (linear, rounded for whole-number controls), as 168n's EngineParam::raw
    float raw (float value01) const noexcept
    {
        const auto v = value01 <= 0.0f ? 0.0f : value01 >= 1.0f ? 1.0f : value01;
        const auto r = low + v * (high - low);
        return integer ? std::round (r) : r;
    }
};

constexpr Range none {};
constexpr Range unit { true, 0.0f, 1.0f, false };
constexpr Range flag { true, 0.0f, 1.0f, true };
constexpr Range ints (float low, float high) { return { true, low, high, true }; }

constexpr Range fruitKick (int i)
{
    constexpr Range r[6] = { ints (-2400, 3600), ints (-1200, 1200), ints (0, 256), ints (0, 256), ints (0, 64), ints (0, 128) };
    return i >= 0 && i < 6 ? r[i] : none;
}

constexpr Range plucked (int i)
{
    constexpr Range r[5] = { ints (0, 256), ints (0, 128), ints (0, 1), ints (0, 1), ints (0, 1) };
    return i >= 0 && i < 5 ? r[i] : none;
}

// Drumpad's 38 (Drumaxx's pads hold the same set at pad·44 + 5)
constexpr Range drumpad (int i)
{
    if (i == 0)
        return flag;
    if (i >= 30 && i <= 33)
        return ints (0, 28); // velocity modulation targets
    return i > 0 && i < 38 ? unit : none;
}

constexpr Range drumaxx (int i)
{
    if (i >= 0 && i < 16 * 44)
    {
        const auto j = i % 44;
        if (j == 0 || j == 1)
            return unit; // volume, pan
        if (j == 2 || j == 3)
            return flag; // mute, solo
        return j >= 5 && j < 43 ? drumpad (j - 5) : none;
    }
    if (i == 0x2c1 || (i >= 0x2c4 && i <= 0x2cb) || i == 0x2cd || i == 0x2ce)
        return unit; // master, EQ bands, limiter gain / release
    return i == 0x2c3 || i == 0x2cc ? flag : none;
}

constexpr Range bassDrum (int i) { return i >= 0 && i < 23 ? unit : none; }

// DrumSynth Live: patch slot·38 + j (120 slots)
constexpr Range drumSynth (int i)
{
    if (i < 0 || i >= 120 * 38)
        return none;
    const auto j = i % 38;
    switch (j)
    {
        case 0: case 35: case 37: return ints (0, 128);
        case 1: return ints (-64, 64);
        case 2: case 3: case 16: return ints (-50, 50);
        case 4: case 10: return ints (0, 1000);
        case 15: return ints (-1, 17);
        case 18: case 23: return ints (0, 100);
        case 22: case 32: return ints (0, 1);
        case 24: return ints (0, 5);
        case 34: case 36: return none;
        default: break;
    }
    if (j >= 25 && j <= 31)
        return ints (-50, 50);
    return ints (0, 127); // 5…9, 11…14, 17, 19…21, 33
}

constexpr Range sakura (int i)
{
    if (i == 7 || i == 12 || i == 21 || i == 23 || (i >= 55 && i <= 63) || i == 70 || i == 74)
        return flag;
    if (i >= 79 && i < 79 + 32)
    {
        const auto j = (i - 79) % 4;
        return j == 0 ? ints (0, 8) : j == 1 ? unit : j == 2 ? flag : ints (0, 14);
    }
    if (i == 111)
        return ints (0, 6);
    if (i == 112)
        return ints (0, 9);
    return (i >= 0 && i < 78) || i == 113 ? unit : none;
}
} // namespace lynx::drumranges
