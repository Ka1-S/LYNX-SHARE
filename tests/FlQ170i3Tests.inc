// Round 170i3: Ogun's release tail, Wave Traveller's older presets (docs/analise/REA_rodada170i3.md)

#include "core/WaveTraveller.h"

namespace flq170i3
{
// the plugin state (event 0xD5) of an FL .fst preset
inline std::vector<std::uint8_t> fstState (const std::filesystem::path& path)
{
    std::ifstream in (path, std::ios::binary);
    const std::vector<std::uint8_t> b ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    if (b.size() < 22 || std::memcmp (b.data(), "FLhd", 4) != 0)
        return {};
    std::size_t p = 8 + (std::size_t) (b[4] | (b[5] << 8) | (b[6] << 16) | (b[7] << 24)) + 8;
    while (p < b.size())
    {
        const auto id = b[p++];
        std::size_t n = id < 64 ? 1 : id < 128 ? 2 : id < 192 ? 4 : 0;
        if (id >= 192)
            for (int shift = 0; p < b.size(); shift += 7)
            {
                const auto c = b[p++];
                n |= (std::size_t) (c & 0x7f) << shift;
                if ((c & 0x80) == 0)
                    break;
            }
        if (p + n > b.size())
            return {};
        if (id == 0xD5)
            return { b.begin() + (long) p, b.begin() + (long) (p + n) };
        p += n;
    }
    return {};
}
} // namespace flq170i3

TEST (flq170i3_wave_traveller_reads_fl_older_header_version)
{
    // FL's own Plugin presets\Generators\Wave Traveller\Default.fst has header version 1000001 (FL writes 1000005);
    // FL's loader drops the version word (FUN_0074aee0), LYNX refused the state
    using namespace lynx;
    if (realFlpDir.empty())
        return;
    const auto folder = std::filesystem::path (realFlpDir) / "Patches" / "Plugin presets" / "Generators" / "Wave Traveller";
    const auto old = flq170i3::fstState (folder / "Default.fst");
    if (old.size() < 12)
        return;
    CHECK (old[8] == 0x41 && old[9] == 0x42 && old[10] == 0x0f); // (1000001)
    wavetrav::State s;
    CHECK (wavetrav::parseState (old, s));
    CHECK (s.selectedKey == 60);
    CHECK (s.samplePath.empty());
    CHECK (std::abs (s.patches[60].speedBeats - 4.0f) < 1e-6f);
    CHECK (s.patches[60].wave.size() == 2);
    // a 1000005 preset still reads as before
    const auto now = flq170i3::fstState (folder / "go.fst");
    CHECK (wavetrav::parseState (now, s));
    CHECK (s.samplePath.find ("VOC_Go.wav") != std::string::npos);
}

TEST (flq170i3_cut_groups_skip_fl_plugin_generators)
{
    // FL's renders of 26 FL generators with "cut itself" equal the ones without (REA_rodada170i3 §2): a note never
    // chokes an FL plugin generator (Ogun's release tail in 169l's project was cut by a Drumaxx note), while the
    // sampler, the audio clip and the LYNX synth in the same group are still cut (REA_rodada167h)
    using namespace lynx;
    Song song;
    const InstrumentKind kinds[] = { InstrumentKind::ogun, InstrumentKind::sytrus, InstrumentKind::threeosc,
                                     InstrumentKind::drumaxx, InstrumentKind::sampler, InstrumentKind::audioClip,
                                     InstrumentKind::synth };
    for (auto k : kinds)
    {
        ChannelDef c;
        c.instrument = k;
        c.cutGroup = c.cutBy = 1;
        song.channels.push_back (c);
    }
    const auto state = RackState::build (song, nullptr, 44100.0);
    CHECK (state != nullptr);
    if (state == nullptr)
        return;
    for (const auto& t : state->cutTargets)
        CHECK ((t == std::vector<int> { 4, 5, 6 }));
}

TEST (flq170i3_ogun_delay_and_reverb_switches_change_the_sound)
{
    // Ogun's delay only runs with S.b358[1] set and its reverb with P[53]'s low byte (designDelay / designReverb): the
    // editor had no switch for them, so on FL's Plugin database Ogun the delay and reverb knobs did nothing
    using namespace lynx;
    if (realFlpDir.empty())
        return;
    const auto db = flq170i3::fstState (std::filesystem::path (realFlpDir) / "Patches" / "Plugin database" / "Generators"
                                        / "Synth special" / "Ogun.fst");
    if (db.empty())
        return;
    auto base = ogunCurrentLayout (db);
    CHECK (ogunSetParam (base, ogunDelayOnIndex, 0) && ogunSetParam (base, ogunReverbOnIndex, 0));
    CHECK (ogunParam (base, ogunDelayOnIndex) == 0 && ogunParam (base, ogunReverbOnIndex) == 0);
    ogun::RefNote n;
    n.key = 60;
    n.release = 8820;
    const int length = 88200;
    auto tail = [&] (const std::vector<std::uint8_t>& st) {
        const auto out = ogun::renderReference (st, { n }, 44100.0, length);
        double e = 0.0;
        for (std::size_t i = out.size() / 2; i < out.size(); ++i)
            e += (double) out[i] * out[i];
        return e;
    };
    const auto dry = tail (base);
    auto delayed = base;
    CHECK (ogunSetParam (delayed, ogunDelayOnIndex, 1) && ogunParam (delayed, ogunDelayOnIndex) == 1);
    CHECK (ogunSetParam (delayed, 26, 100) && ogunSetParam (delayed, 27, 192) && ogunSetParam (delayed, 29, 120));
    CHECK (ogunStateUsable (delayed));
    const auto withDelay = tail (delayed);
    auto synced = delayed;
    CHECK (ogunSetParam (synced, ogunDelayTempoIndex, 1) && ogunParam (synced, ogunDelayTempoIndex) == 1);
    const auto withSync = tail (synced);
    auto reverb = base;
    const auto p53 = ogunParam (db, 53).value_or (0);
    CHECK (ogunSetParam (reverb, ogunReverbOnIndex, 1) && ogunParam (reverb, ogunReverbOnIndex) == 1);
    CHECK (ogunSetParam (reverb, 39, 120));
    const auto withReverb = tail (reverb);
    std::printf ("  Ogun tail energy: dry %.3g, delay %.3g, delay synced %.3g, reverb %.3g (P53 was %d)\n", dry, withDelay,
                 withSync, withReverb, p53);
    CHECK (withDelay > 1.5 * dry + 1e-6);
    CHECK (std::abs (withSync - withDelay) > 1e-3 * withDelay);
    CHECK (withReverb > 1.5 * dry + 1e-6);
}
