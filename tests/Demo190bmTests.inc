// Round 190bm: LYNX's demo project (core/ShowcaseSong.h; Demo.lynx in the installers). A 0.1.25 report: "the lead at
// the end is way too loud" and "the bass is two octaves too low". The bass plays in a bass register, and no section of
// the song is far louder than the drop.

#include "core/ShowcaseSong.h"

namespace demo190bm
{
inline double hzOf (double key) { return 440.0 * std::pow (2.0, (key - 69.0) / 12.0); }

// RMS (dB) of the song's bars [from, to) in a whole-song render
inline std::vector<double> sectionLevels (const lynx::Song& showcase, const std::vector<std::pair<int, int>>& sections)
{
    constexpr int rate = 44100, block = 256;
    std::int64_t songTicks = 0;
    const auto song = lynx::makeExportSong (showcase, true, 0, songTicks);
    const auto tps = lynx::ticksPerSample (song.bpm, rate);
    auto state = lynx::RackState::build (song, nullptr, rate);
    lynx::RackRenderer renderer;
    renderer.prepare (rate, block);
    const auto total = (std::size_t) ((double) songTicks / tps);
    std::vector<float> l (total), r (total);
    double tick = 0.0;
    for (std::size_t pos = 0; pos + block <= total; pos += block)
    {
        renderer.process (state.get(), true, tick, block, l.data() + pos, r.data() + pos);
        tick += block * tps;
    }
    std::vector<double> levels;
    const auto bar = (double) lynx::ppq * 4.0;
    for (const auto& [from, to] : sections)
    {
        const auto a = (std::size_t) (from * bar / tps), b = std::min (total, (std::size_t) (to * bar / tps));
        double sum = 0.0;
        for (std::size_t i = a; i < b; ++i)
            sum += 0.5 * ((double) l[i] * l[i] + (double) r[i] * r[i]);
        levels.push_back (10.0 * std::log10 (std::max (1e-12, sum / (double) std::max<std::size_t> (1, b - a))));
    }
    return levels;
}
} // namespace demo190bm

TEST (demo190bm_bass_plays_in_a_bass_register)
{
    const auto song = lynx::makeShowcaseSong();
    int bass = -1;
    for (std::size_t c = 0; c < song.channels.size(); ++c)
        if (song.channels[c].name == "Reese Bass")
            bass = (int) c;
    CHECK (bass >= 0);
    if (bass < 0)
        return;
    int notes = 0;
    for (const auto& pattern : song.patterns)
        for (const auto& n : pattern.notes)
        {
            if (n.channel != bass)
                continue;
            ++notes;
            double loudest = 0.0, loudestHz = 0.0, lowestHz = 1.0e9;
            for (const auto& osc : song.channels[(std::size_t) bass].synth.osc)
            {
                if (osc.level <= 0.0f)
                    continue;
                const auto hz = demo190bm::hzOf (n.key + 12.0 * osc.octave + osc.semitones);
                lowestHz = std::min (lowestHz, hz);
                if (osc.level > loudest)
                {
                    loudest = osc.level;
                    loudestHz = hz;
                }
            }
            // the main oscillators between E1 (41.2 Hz) and C4 (the line's octave jumps); the sub not under E1
            CHECK (loudestHz >= 82.0 && loudestHz <= 262.0);
            CHECK (lowestHz >= 41.0);
        }
    CHECK (notes == 28);
}

TEST (demo190bm_no_section_is_far_louder_than_the_drop)
{
    // bars: the drop's two halves, the breakdown, the final drop (with the lead)
    const auto levels = demo190bm::sectionLevels (lynx::makeShowcaseSong(), { { 8, 12 }, { 12, 16 }, { 16, 20 }, { 20, 24 } });
    const auto drop = levels[0];
    CHECK (drop > -20.0 && drop < -8.0);
    for (const auto level : levels)
        CHECK (std::abs (level - drop) < 2.0); // (it was +6.1 dB RMS, +9.6 LU, in the final drop up to 0.1.25)
}
