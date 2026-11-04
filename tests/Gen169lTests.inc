// Round 169l: Drumaxx's level through FL's host pan law, a stateless FL Drumaxx, new generators' default state
// (REA_rodada169l)

#include "core/Drumaxx.h"
#include "core/NativeEngines.h"

TEST (gen169l_probe_new_generator_presets)
{
    using namespace lynx;
    if (realFlpDir.empty())
        return;
    const auto fl = natives142::flAccess();
    for (const auto& e : natives::instrumentEngines())
    {
        const auto c = natives::presetCandidates (e.presetFolder, true, fl);
        std::string used;
        int tries = 0;
        for (const auto& p : c)
        {
            if (++tries > 60)
                break;
            if (natives::channelFromPresetFile (e.kind, p, fl)) { used = p; break; }
        }
        std::string db;
        for (const auto& p : fl.listPresets (realFlpDir + "/Patches/Plugin database/Generators"))
            if (std::filesystem::path (p).filename().string() == std::string (e.presetFolder) + ".fst")
                db = p;
        const auto dbOk = ! db.empty() && natives::channelFromPresetFile (e.kind, db, fl).has_value();
        std::printf ("  PROBE %-18s used=%s db=%s %s\n", e.name, used.c_str(), db.c_str(), dbOk ? "READABLE" : "no");
    }
}

namespace gen169l
{
// one Drumaxx instrument (settled output gain), silence for `lead` samples, then key 36 at Init.Vol 1; L/R interleaved
inline std::vector<float> playInstrument (const lynx::ChannelDef& c, float gl, float gr, int lead, int length)
{
    auto in = lynx::makeDrumaxxInstrument();
    in->prepare (44100.0);
    in->configure (c, 120.0);
    std::vector<float> l ((std::size_t) length, 0.0f), r ((std::size_t) length, 0.0f);
    in->render (l.data(), r.data(), lead, gl, gr);
    in->noteOn (36, 100.0f / 128.0f); // FL's default velocity: Init.Vol 1.0
    for (int at = lead; at < length; at += 512)
        in->render (l.data() + at, r.data() + at, std::min (512, length - at), gl, gr);
    std::vector<float> out (2 * (std::size_t) length);
    for (int i = 0; i < length; ++i)
        out[2 * (std::size_t) i] = l[(std::size_t) i], out[2 * (std::size_t) i + 1] = r[(std::size_t) i];
    return out;
}
} // namespace gen169l

// FL hands Drumaxx's per-key tables the host's ComputeLRVol of (Final.Pan, Final.Vol): Vol·cos/sin((1 + pan)π/4), so a
// centred channel plays at 0.7071·Vol per side (REA_rodada169l §1; LYNX was +3.0 dB over FL's render)
TEST (gen169l_drumaxx_through_fl_host_pan_law)
{
    using namespace lynx;
    const auto c = banks::defaultChannel (InstrumentKind::drumaxx, "Drumaxx");
    CHECK (c.has_value());
    if (! c)
        return;
    const int lead = 8192, length = lead + 44100;
    const auto reference = [&] (double finalVol) {
        drumaxx::RefNote n;
        n.start = lead, n.key = 36, n.initVol = 1.0, n.finalVol = finalVol;
        return drumaxx::renderReference (c->pluginState, { n }, 44100.0, length, 512);
    };
    const auto compare = [&] (const std::vector<float>& got, const std::vector<float>& want, int side) {
        double peak = 0.0, err = 0.0;
        for (int i = lead; i < length; ++i)
        {
            peak = std::max (peak, (double) std::abs (want[2 * (std::size_t) i + (std::size_t) side]));
            err = std::max (err, (double) std::abs (got[2 * (std::size_t) i + (std::size_t) side] - want[2 * (std::size_t) i + (std::size_t) side]));
        }
        return peak > 1e-3 ? err / peak : 1.0;
    };
    // the centre: LYNX's channel gains are √2·V·cos/sin((1 + p)π/4) = V at p = 0 (V = 0.55, FL's default channel volume)
    const auto V = (double) channelGain (0.78125f);
    const auto centre = gen169l::playInstrument (*c, (float) V, (float) V, lead, length);
    const auto half = reference (V * 0.70710678);
    CHECK (compare (centre, half, 0) < 1e-5 && compare (centre, half, 1) < 1e-5);
    CHECK (compare (centre, reference (V), 0) > 0.2); // (not the old Vol on both sides: +3 dB)
    // the channel's pan counts (it does not in round 67's harness stand-in): p = −0.5 → cos(π/8), sin(π/8)
    const auto t = 0.25 * 3.14159265358979;
    const auto left = gen169l::playInstrument (*c, (float) (std::sqrt (2.0) * V * std::cos (t / 2.0 + 0.0)), (float) (std::sqrt (2.0) * V * std::sin (t / 2.0)), lead, length);
    CHECK (compare (left, reference (V * std::cos (t / 2.0)), 0) < 1e-5);
    CHECK (compare (left, reference (V * std::sin (t / 2.0)), 1) < 1e-5);
}

// FL renders a Drumaxx channel with no state (the DLL's own defaults) silent (REA_rodada169e §2): LYNX imports it as a
// Drumaxx without a kit, silent, not as the stand-in synth; an unreadable non-empty state stays unsupported
TEST (gen169l_stateless_fl_drumaxx_is_silent)
{
    using namespace lynx;
    FlpBuilder b;
    b.ascii (0xC7, "25.2.5.5319");
    for (std::uint16_t i = 0; i < 2; ++i)
    {
        b.u8 (0x40); b.u16 (i);
        b.u8 (0x15); b.u8 (2);
        b.utf16 (0xC9, u"Drumaxx");
        if (i == 1)
            b.data (0xD5, std::vector<std::uint8_t> (40, 7));
    }
    const auto song = importFlp (flp::parseProject (b.bytes));
    CHECK (song.channels.size() == 2u);
    if (song.channels.size() != 2u)
        return;
    CHECK (song.channels[0].instrument == InstrumentKind::drumaxx && song.channels[0].pluginState.empty());
    CHECK (song.channels[1].instrument != InstrumentKind::drumaxx);
    for (const auto key : { 0, 36, 48, 60, 100 })
    {
        auto in = makeInstrument (InstrumentKind::drumaxx);
        in->prepare (44100.0);
        in->configure (song.channels[0], 120.0);
        in->noteOn (key, 0.8f);
        std::vector<float> l (22050), r (22050);
        in->render (l.data(), r.data(), 22050, 1.0f, 1.0f);
        float peak = 0.0f;
        for (std::size_t i = 0; i < l.size(); ++i)
            peak = std::max ({ peak, std::abs (l[i]), std::abs (r[i]) });
        CHECK (peak == 0.0f);
    }
}
