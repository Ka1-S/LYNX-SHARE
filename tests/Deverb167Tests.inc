// --- Edison's Deverb (core/AudioEditTools.h deverb) - round 167c (REA_rodada167c.md §3)

namespace deverb167
{
// noise bursts (`burst` s every `period` s) in a room: a velvet-noise impulse response decaying to -60 dB in `t60` s
struct Room
{
    lynx::audioedit::Document dry, wet;
};
inline Room make (double t60, double rate = 44100.0, double seconds = 4.0, double period = 0.8, double burst = 0.12)
{
    Room r;
    const auto n = (int) (rate * seconds);
    r.dry.audio = lynx::audioedit::silence (1, rate, n);
    std::uint32_t seed = 12345;
    auto rnd = [&seed] {
        seed = seed * 1664525u + 1013904223u;
        return (double) (seed >> 8) / (double) (1u << 24) * 2.0 - 1.0;
    };
    for (int i = 0; i < n; ++i)
        if (std::fmod (i / rate, period) < burst)
            r.dry.audio.channels[0][(std::size_t) i] = (float) (0.05 * rnd());
    // the impulse response: the direct sound, then 2000 taps/s from 5 ms on, each ±e^(-6.9 t / t60)
    std::vector<std::pair<int, double>> taps { { 0, 1.0 } };
    const auto spacing = rate / 2000.0;
    for (double t = 0.005 * rate; t < t60 * 1.2 * rate; t += spacing)
    {
        const auto at = (int) (t + (rnd() * 0.5 + 0.5) * (spacing - 1));
        taps.emplace_back (at, (rnd() > 0 ? 0.35 : -0.35) * std::exp (-6.907755 * (at / rate) / t60));
    }
    r.wet.audio = lynx::audioedit::silence (1, rate, n);
    const auto& x = r.dry.audio.channels[0];
    auto& y = r.wet.audio.channels[0];
    for (int i = 0; i < n; ++i)
        if (x[(std::size_t) i] != 0.0f)
            for (const auto& [d, g] : taps)
                if (i + d < n)
                    y[(std::size_t) (i + d)] += (float) (g * x[(std::size_t) i]);
    return r;
}

// the energy (dB) of the parts `from`..`to` s after each burst's start
inline double energyDb (const lynx::audioedit::Audio& a, double from, double to, double period = 0.8)
{
    double e = 0.0;
    for (int i = 0; i < a.frames(); ++i)
    {
        const auto t = std::fmod (i / a.sampleRate, period);
        if (i / a.sampleRate > period && t >= from && t < to)
            e += (double) a.channels[0][(std::size_t) i] * a.channels[0][(std::size_t) i];
    }
    return 10.0 * std::log10 (e + 1.0e-20);
}
} // namespace deverb167

TEST (deverb167_reverb_time)
{
    using namespace lynx::audioedit;
    // the room's T60 read from the free decays after each burst
    for (const double t60 : { 0.5, 1.0 })
    {
        const auto room = deverb167::make (t60);
        const auto est = estimateReverbTime (room.wet.audio, { 0, room.wet.audio.frames() });
        CHECK (est > t60 * 0.7 && est < t60 * 1.3);
        if (! (est > t60 * 0.7 && est < t60 * 1.3))
            std::printf ("  T60 %.2f read as %.3f\n", t60, est);
    }
    // a dry signal has no decays to read
    const auto room = deverb167::make (0.5);
    CHECK (estimateReverbTime (room.dry.audio, { 0, room.dry.audio.frames() }) < 0.1 + 1e-9);
}

TEST (deverb167_removes_the_tail)
{
    using namespace lynx::audioedit;
    const auto room = deverb167::make (1.0);
    for (const bool voice : { false, true })
    {
        auto d = room.wet;
        DeverbSettings s;
        s.voice = voice;
        deverb (d, {}, s);
        // the tail between the bursts drops a lot, the bursts far less (in this very live room most of a burst's
        // energy is already reverberation: 10 dB over the dry sound)
        const auto tailBefore = deverb167::energyDb (room.wet.audio, 0.3, 0.75), tailAfter = deverb167::energyDb (d.audio, 0.3, 0.75);
        const auto burstBefore = deverb167::energyDb (room.wet.audio, 0.0, 0.12), burstAfter = deverb167::energyDb (d.audio, 0.0, 0.12);
        CHECK (tailBefore - tailAfter > (voice ? 15.0 : 10.0));
        CHECK (burstBefore - burstAfter < 5.0 && (tailBefore - tailAfter) - (burstBefore - burstAfter) > 10.0);
        std::printf ("  deverb voice=%d: tail %.1f dB, bursts %.1f dB\n", (int) voice, tailAfter - tailBefore, burstAfter - burstBefore);
    }
    // FL's mix: -1 = the original, 1 = processed, 0 = half and half
    auto none = room.wet, dry = room.wet, half = room.wet;
    DeverbSettings s;
    s.mix = -1.0;
    deverb (none, {}, s);
    s.mix = 1.0;
    deverb (dry, {}, s);
    s.mix = 0.0;
    deverb (half, {}, s);
    double worst = 0.0, worstHalf = 0.0, peakIn = 0.0, peakOut = 0.0;
    for (int i = 0; i < room.wet.audio.frames(); ++i)
    {
        const auto w = room.wet.audio.channels[0][(std::size_t) i];
        worst = std::max (worst, (double) std::abs (none.audio.channels[0][(std::size_t) i] - w));
        worstHalf = std::max (worstHalf, (double) std::abs (half.audio.channels[0][(std::size_t) i] - 0.5f * (dry.audio.channels[0][(std::size_t) i] + w)));
        peakIn = std::max (peakIn, (double) std::abs (w));
        peakOut = std::max (peakOut, (double) std::abs (dry.audio.channels[0][(std::size_t) i]));
    }
    CHECK (worst < 1e-6 && worstHalf < 1e-5);
    // the processed sound gets the original's peak back
    CHECK (peakIn < 1.0 && std::abs (peakOut - peakIn) < 1e-4);
    // a selection: only it changes
    auto part = room.wet;
    deverb (part, Range { 44100, 88200 }, DeverbSettings {});
    CHECK (part.audio.channels[0][44099] == room.wet.audio.channels[0][44099] && part.audio.channels[0][88200] == room.wet.audio.channels[0][88200]);
    CHECK (part.audio.channels[0][70000] != room.wet.audio.channels[0][70000]);
}
