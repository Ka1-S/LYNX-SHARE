// Round 169l: Drumaxx's level through FL's host pan law, a stateless FL Drumaxx, new generators' default state
// (REA_rodada169l)

#include "core/Drumaxx.h"
#include "core/NativeEngines.h"
#include "core/Inflate.h"
#include "core/Ogun.h"

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
    auto c = banks::defaultChannel (InstrumentKind::drumaxx, "Drumaxx");
    CHECK (c.has_value());
    if (! c)
        return;
    drumaxxSetParam (c->pluginState, 0x2cc, 0.0f); // (the master limiter off: the rest of the chain is linear in the gains)
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
    CHECK (compare (centre, half, 0) < 1e-6 && compare (centre, half, 1) < 1e-6);
    CHECK (compare (centre, reference (V), 0) > 0.2); // (not the old Vol on both sides: +3 dB)
    // the channel's pan counts (it does not in round 67's harness stand-in): p = −0.5 → cos(π/8), sin(π/8)
    const auto t = 0.25 * 3.14159265358979;
    const auto left = gen169l::playInstrument (*c, (float) (std::sqrt (2.0) * V * std::cos (t / 2.0 + 0.0)), (float) (std::sqrt (2.0) * V * std::sin (t / 2.0)), lead, length);
    CHECK (compare (left, reference (V * std::cos (t / 2.0)), 0) < 1e-6);
    CHECK (compare (left, reference (V * std::sin (t / 2.0)), 1) < 1e-6);
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

// Ogun states of versions 10…13 (FL's Plugin database Ogun is version 11; REA_rodada169l §4): the loader reads 12 bytes,
// then 0xf4 − 1 (v < 11) − 1 (v < 12) − 6 (v < 13) bytes of the parameter block, the rest set by FUN_00864d00
TEST (gen169l_ogun_old_state_versions)
{
    using namespace lynx;
    const auto state = ogunTestState(); // (version 18: a one-byte comment count, 0)
    const auto body = inflateZlib (state.data() + 4, state.size() - 4, false, 16u << 20);
    CHECK (body.has_value() && body->size() > 0x110 && (*body)[0x10f] == 0);
    if (! body || body->size() <= 0x110)
        return;
    ChannelDef today;
    today.instrument = InstrumentKind::ogun;
    today.pluginState = state;
    const auto render = [] (const ChannelDef& ch) {
        auto in = makeInstrument (InstrumentKind::ogun);
        in->prepare (44100.0);
        in->configure (ch, 120.0);
        in->noteOn (60, 0.78125f);
        std::vector<float> l (8192), r (8192);
        for (int at = 0; at < 8192; at += 512)
            in->render (l.data() + at, r.data() + at, 512, 1.0f, 1.0f);
        return l;
    };
    const auto reference = render (today);
    for (const int v : { 10, 11, 12, 13 })
    {
        // the same patch stored the old way: the block cut to what that version's loader reads, a u32 comment count
        const std::size_t n = 12 + 0xf4 - (v < 11 ? 1u : 0u) - (v < 12 ? 1u : 0u) - (v < 13 ? 6u : 0u);
        std::vector<std::uint8_t> oldBody (body->begin(), body->begin() + (std::ptrdiff_t) n);
        oldBody.insert (oldBody.end(), body->begin() + 0x100, body->begin() + 0x10f);
        oldBody.insert (oldBody.end(), { 0, 0, 0, 0 });
        oldBody.insert (oldBody.end(), body->begin() + 0x110, body->end());
        std::vector<std::uint8_t> old { (std::uint8_t) v, 0, 0, 0 };
        const auto z = zlibStored (oldBody);
        old.insert (old.end(), z.begin(), z.end());
        CHECK (ogunStateUsable (old));
        const auto now = ogunCurrentLayout (old);
        CHECK (now.size() > 8 && now[0] == 14 && ogunStateUsable (now));
        const auto back = inflateZlib (now.data() + 4, now.size() - 4, false, 16u << 20);
        CHECK (back.has_value() && back->size() == body->size() + 3);
        if (! back || back->size() != body->size() + 3)
            continue;
        // what the old state stores is kept; the bytes it lacks are the loader's own values
        CHECK (std::equal (back->begin(), back->begin() + (std::ptrdiff_t) n, body->begin()));
        CHECK (std::equal (back->begin() + 0x113, back->end(), body->begin() + 0x110));
        std::int32_t dust = 0;
        std::memcpy (&dust, back->data() + 0xfc, 4);
        CHECK ((n <= 0xfc ? dust == 430 : std::memcmp (back->data() + 0xfc, body->data() + 0xfc, 4) == 0) && (*back)[0xfa] == 0);
        CHECK (ogunParam (now, 17) == 180 && ogunParam (now, 52) == 9);
        auto oldChannel = today;
        oldChannel.pluginState = now;
        const auto got = render (oldChannel);
        double e = 0.0;
        for (const auto x : got)
            e += (double) x * x;
        CHECK (e > 1e-6); // (it sounds)
    }
    CHECK (! ogunStateUsable (std::vector<std::uint8_t> { 9, 0, 0, 0, 0x78, 0x9c, 0, 0 })); // (older versions: not read)
}

// With FL installed, a new GMS, Morphine, Transistor Bass, Sakura or Ogun is FL's Plugin database state (FL's picker loads
// the entry's .fst into the new channel, REA_rodada169l §3), like Drumaxx, Drumpad and BassDrum (round 169e)
TEST (gen169l_new_generators_are_fl_plugin_database_state)
{
    using namespace lynx;
    if (realFlpDir.empty())
        return;
    const auto fl = natives142::flAccess();
    for (const auto kind : { InstrumentKind::gms, InstrumentKind::morphine, InstrumentKind::transistorbass, InstrumentKind::sakura,
                             InstrumentKind::ogun, InstrumentKind::drumaxx, InstrumentKind::drumpad, InstrumentKind::bassdrum })
    {
        const auto* e = natives::instrumentEngine (kind);
        CHECK (e != nullptr);
        if (e == nullptr)
            continue;
        const auto c = natives::presetCandidates (e->presetFolder, true, fl);
        const auto first = c.empty() ? std::string() : c.front();
        const auto fromDb = first.find ("Plugin database") != std::string::npos
                            && std::filesystem::path (first).filename().string() == std::string (e->presetFolder) + ".fst";
        if (! fromDb)
            std::printf ("  %s starts from %s\n", e->name, first.c_str());
        CHECK (fromDb);
        const auto made = natives::makeChannel (kind, fl);
        const auto fromFile = natives::channelFromPresetFile (kind, first, fl);
        CHECK (made.has_value() && fromFile.has_value());
        if (made && fromFile)
        {
            // (Sakura and Drumpad keep their parameters in modelParams, the others the raw state)
            CHECK (made->instrument == kind && made->pluginState == fromFile->pluginState && made->modelParams == fromFile->modelParams
                   && (! made->pluginState.empty() || ! made->modelParams.empty()));
        }
    }
    // Ogun: FL's version-11 state, read (it wasn't before) and kept in today's layout
    const auto og = natives::makeChannel (InstrumentKind::ogun, fl);
    CHECK (og.has_value() && og->pluginState.size() > 8 && og->pluginState[0] == 14);
    // (since round 169l2 every generator starts from its Plugin database entry: Gen169l2Tests.inc)
}
