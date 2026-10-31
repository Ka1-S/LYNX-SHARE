#include "core/Dvs.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

namespace lynx::dvs
{
namespace
{
// [DECOMP engine_x64 0x1800037c0] the five formats of FL's sine-and-amplitude decoder, by the state's timecode index
const Format seratoA { 20, 0xa5411u, 0xa786cu, 710000u, 1000, 25.7, 0x00 };
const Format seratoB { 20, 0xd76c6u, 0x9b0f2u, 940000u, 1000, 8.5, 0x00 };
const Format seratoCd { 20, 0x30aefu, 0x34d54u, 982000u, 1000, 0.0, 0x00 };
const Format traktorA { 23, 0x759c41u, 0x410410u, 1500001u, 2000, 0.0, 0x70 };
const Format traktorB { 23, 0x53e4f7u, 0x410410u, 2109993u, 2000, 0.0, 0x70 };

int parity (std::uint32_t x) noexcept
{
    int p = 0;
    for (; x != 0; x >>= 1)
        p ^= (int) (x & 1u);
    return p;
}

// The position table: (register << 32 | cycle) sorted by register
struct Table
{
    std::vector<std::uint64_t> entries;
    bool find (std::uint32_t state, std::uint32_t& cycle) const noexcept
    {
        const auto key = (std::uint64_t) state << 32;
        const auto it = std::lower_bound (entries.begin(), entries.end(), key);
        if (it == entries.end() || (*it >> 32) != state)
            return false;
        cycle = (std::uint32_t) *it;
        return true;
    }
};

constexpr int slots = 8;
std::atomic<const Table*> tables[slots] {};
std::atomic<bool> building[slots] {};

const Table* tableOf (int state) noexcept
{
    return state >= 0 && state < slots ? tables[state].load (std::memory_order_acquire) : nullptr;
}

void build (int state)
{
    const auto* f = format (state);
    auto* t = new Table();
    t->entries.reserve (f->length);
    auto s = f->seed;
    for (std::uint32_t i = 0; i < f->length; ++i)
    {
        t->entries.push_back ((std::uint64_t) s << 32 | i);
        s = nextState (s, *f);
    }
    std::sort (t->entries.begin(), t->entries.end());
    tables[state].store (t, std::memory_order_release); // kept for the program's life (shared by every decoder)
}
} // namespace

int stateOfCombo (int combo) noexcept
{
    // [DECOMP Fruity Scratcher 0x733fe0] Off → −1, Timecode CD (the last entry) → 0, the others keep their index
    if (combo <= 0 || combo >= comboCount)
        return -1;
    return combo == comboCount - 1 ? 0 : combo;
}

int comboOfState (int state) noexcept
{
    if (state == 0)
        return comboCount - 1;
    return state >= 1 && state < comboCount - 1 ? state : 0;
}

const char* comboName (int combo) noexcept
{
    static const char* const names[comboCount] = { "Off",
                                                    "Serato Mk2, Side A",
                                                    "Serato Mk2, Side B",
                                                    "Serato CD",
                                                    "Traktor Skratch Mk1, Side A",
                                                    "Traktor Skratch Mk1, Side B",
                                                    "Traktor Skratch Mk2, Side A",
                                                    "Traktor Skratch Mk2, Side B",
                                                    "Timecode CD" };
    return combo >= 0 && combo < comboCount ? names[combo] : names[0];
}

const Format* format (int state) noexcept
{
    // [DECOMP engine_x64 createTimecodeDecoder 0x180001080]: 1 → Serato A (id 1), 2 → Serato B (id 0), 3 → Serato
    // CD (id 2), 4 / 5 → Traktor Mk1 A / B (ids 3 / 4); 6 / 7 = the Traktor Mk2 decoder, 0 = the CD decoder
    switch (state)
    {
        case 1: return &seratoA;
        case 2: return &seratoB;
        case 3: return &seratoCd;
        case 4: return &traktorA;
        case 5: return &traktorB;
        default: return nullptr;
    }
}

bool supported (int state) noexcept
{
    return format (state) != nullptr;
}

std::uint32_t nextState (std::uint32_t state, const Format& f) noexcept
{
    const auto mask = (1u << f.bits) - 1u;
    return ((state << 1) & mask) | (std::uint32_t) parity (state & f.taps);
}

void warm (int state, bool wait)
{
    if (format (state) == nullptr)
        return;
    if (tableOf (state) == nullptr && ! building[state].exchange (true))
    {
        if (wait)
            build (state);
        else
            std::thread ([state] { build (state); }).detach();
    }
    if (wait)
        while (tableOf (state) == nullptr)
            std::this_thread::sleep_for (std::chrono::milliseconds (2));
}

// ------------------------------------------------------------------------------------------------------- decoder
void Decoder::Crossing::feed (float x, float k) noexcept
{
    // [DECOMP 0x180003c10] a zero crossing with 0.004 of hysteresis around a fast-moving zero (the signal's own
    // low frequencies: rumble and DC follow the record)
    crossed = false;
    if (! positive)
    {
        if (x > zero + 0.004f)
            positive = crossed = true;
    }
    else if (x < zero - 0.004f)
    {
        positive = false;
        crossed = true;
    }
    zero += (x - zero) * k;
}

void Decoder::configure (int state, double sampleRate) noexcept
{
    fmt = format (state);
    stateIndex = state;
    rate = sampleRate > 0.0 ? sampleRate : 44100.0;
    dt = 1.0 / rate;
    // [DECOMP 0x1800037c0] the zero's time constant: 1/900 s
    zeroK = (float) (1.0 - std::exp (-1.0 / (rate / 900.0)));
    reset();
}

void Decoder::reset() noexcept
{
    primary = {};
    secondary = {};
    dirForward = true;
    reg = 0;
    valid = 0;
    refLevel = 1.0f;
    filtX = velocity = 0.0;
    pos = 0.0;
    lockCount = 0;
}

void Decoder::shiftIn (bool bit) noexcept
{
    // [DECOMP 0x180003aa0] the bit enters the register at the end the needle moves towards; 25 bits in a row that
    // follow the register's rule name a place on the record
    const auto& f = *fmt;
    const auto mask = (1u << f.bits) - 1u;
    std::uint32_t predicted, next;
    if (dirForward)
    {
        predicted = ((reg << 1) & mask) | (std::uint32_t) parity (reg & f.taps);
        next = ((reg << 1) & mask) | (bit ? 1u : 0u);
    }
    else
    {
        predicted = (std::uint32_t) parity ((((f.taps << 1) & mask) | 1u) & reg) << (f.bits - 1) | reg >> 1;
        next = (bit ? 1u : 0u) << (f.bits - 1) | reg >> 1;
    }
    reg = next;
    if (predicted != next)
    {
        valid = 0;
        return;
    }
    if (++valid <= 24)
        return;
    const auto* table = tableOf (stateIndex);
    std::uint32_t cycle = 0;
    if (table == nullptr || ! table->find (next, cycle))
        return;
    // [DECOMP 0x180003e80] the place the register names; the running position jumps there when it is 0.1 s away
    const auto named = (double) cycle / (double) f.resolution - f.offset;
    if (std::abs (named - pos) > 0.1)
        pos = named;
    ++lockCount;
}

void Decoder::process (float left, float right) noexcept
{
    if (fmt == nullptr)
        return;
    const auto& f = *fmt;
    // [DECOMP 0x180003df0] the primary channel is the right one unless the format says left
    const bool leftPrimary = (f.flags & 0x40) != 0;
    primary.feed (leftPrimary ? left : right, zeroK);
    secondary.feed (leftPrimary ? right : left, zeroK);

    double step = 0.0;
    if (primary.crossed || secondary.crossed)
    {
        // the order of the two channels' crossings gives the direction
        bool fwd = primary.crossed ? primary.positive != secondary.positive : primary.positive == secondary.positive;
        if ((f.flags & 0x10) != 0)
            fwd = ! fwd;
        if (fwd != dirForward)
        {
            dirForward = fwd;
            valid = 0;
        }
        step = 0.25 / (double) f.resolution; // a quarter of a cycle per crossing
    }
    if (! dirForward)
        step = -step;

    // the speed tracker (alpha 1/512, beta 1/131072 per sample)
    const auto predicted = dt * velocity + filtX;
    const auto err = step - predicted;
    filtX = (err * 0.001953121185 + predicted) - step;
    velocity += err * 7.62937963e-06 / dt;

    // a bit on each peak of the primary channel (the secondary's crossing): loud = 1, quiet = 0
    const bool wantPositive = (f.flags & 0x20) == 0;
    if (secondary.crossed && primary.positive == wantPositive)
    {
        const auto level = std::abs ((leftPrimary ? left : right) - primary.zero);
        shiftIn (level > refLevel);
        refLevel += (level - refLevel) * (1.0f / 48.0f);
    }

    // [DECOMP 0x180003f10] the running position follows the speed
    pos += velocity / rate;
}
} // namespace lynx::dvs
